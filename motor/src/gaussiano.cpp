// gaussiano.cpp
// -----------------------------------------------------------------------------
// Implementacion del desenfoque gaussiano. Ver gaussiano.hpp para el resumen.
//
// IDEA CLAVE para que paralelo == secuencial byte a byte:
//   El calculo de UNA fila de cada pasada vive en una funcion (pasada_h_fila,
//   pasada_v_fila). La version secuencial llama a esas funciones en un for
//   normal y la paralela las llama dentro de un "omp parallel for". Como cada
//   fila se calcula con el mismo codigo, las mismas operaciones y en el mismo
//   orden, el resultado es identico sin importar cuantos hilos haya. Ademas no
//   hay reducciones: cada hilo escribe filas distintas, nunca la misma celda.
// -----------------------------------------------------------------------------
#include "gaussiano.hpp"

#include <cmath>
#include <cstddef>
#include <memory>

namespace estencil {
namespace {

// Nucleo binomial 1/16 * [1 4 6 4 1]. Todos son potencias de 2 divididas entre
// 16, asi que se representan EXACTAMENTE en double. Consecuencia curiosa: con
// entradas enteras de 8 bits todas las sumas y productos de abajo son exactos,
// por eso la forma directa 5x5 y la separable dan los mismos bytes.
constexpr double K[5] = {1.0 / 16, 4.0 / 16, 6.0 / 16, 4.0 / 16, 1.0 / 16};

// Recorta v al rango [lo, hi]: es la convencion de borde "repetir el pixel
// mas cercano". Si x = -2 se usa x = 0; si x = W+1 se usa x = W-1.
inline int recortar(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// Indice lineal (fila y, columna x) en una imagen almacenada por filas.
// Se hace en size_t para no desbordar int con imagenes grandes.
inline std::size_t idx(int y, int x, int ancho) {
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(ancho) +
           static_cast<std::size_t>(x);
}

// Redondeo al entero mas cercano con empates hacia arriba, saturado a [0,255].
// floor(v + 0.5) manda 2.5 -> 3 (empate hacia arriba).
inline std::uint8_t a_byte(double v) {
    double r = std::floor(v + 0.5);
    if (r < 0.0) r = 0.0;
    if (r > 255.0) r = 255.0;
    return static_cast<std::uint8_t>(r);
}

// PASADA HORIZONTAL, una fila:  T(x,y) = sum_{i=-2..2} k_i * I(x+i, y)
// Lee la fila de la entrada (bytes) y escribe la fila de T (double: aqui NO se
// redondea, para que el resultado coincida con la forma directa).
void pasada_h_fila(const std::uint8_t* entrada, int ancho, int y, double* T) {
    const std::uint8_t* fila = entrada + idx(y, 0, ancho);
    double* t = T + idx(y, 0, ancho);
    for (int x = 0; x < ancho; ++x) {
        double s = 0.0;
        for (int i = -2; i <= 2; ++i) {
            s += K[i + 2] * fila[recortar(x + i, 0, ancho - 1)];
        }
        t[x] = s;
    }
}

// PASADA VERTICAL, una fila:  O(x,y) = round( sum_{j=-2..2} k_j * T(x, y+j) )
// Se toman los punteros a las 5 filas de T involucradas UNA vez por fila (con
// el recorte de borde ya aplicado) y el ciclo interno recorre x. Asi cada
// puntero avanza de forma contigua en memoria (5 flujos secuenciales). Si en
// cambio el ciclo interno recorriera y, cada paso saltaria una fila completa
// (ancho*8 bytes) y traeria una linea de cache nueva por cada lectura.
void pasada_v_fila(const double* T, int ancho, int alto, int y,
                   std::uint8_t* salida) {
    const double* r[5];
    for (int j = -2; j <= 2; ++j) {
        r[j + 2] = T + idx(recortar(y + j, 0, alto - 1), 0, ancho);
    }
    std::uint8_t* o = salida + idx(y, 0, ancho);
    for (int x = 0; x < ancho; ++x) {
        double s = 0.0;
        for (int j = 0; j < 5; ++j) {
            s += K[j] * r[j][x];
        }
        o[x] = a_byte(s);  // unico redondeo de todo el filtro
    }
}

// Reserva ancho*alto doubles SIN inicializarlos. Con std::vector<double> el
// constructor pondria todo a cero en el hilo principal (una zona secuencial de
// ~130 MB en 4096x4096 que arruinaria el speedup). Sin inicializar, la primera
// escritura de cada pagina ocurre dentro del ciclo paralelo ("first touch").
std::unique_ptr<double[]> reservar_intermedia(int ancho, int alto) {
    return std::unique_ptr<double[]>(
        new double[static_cast<std::size_t>(ancho) *
                   static_cast<std::size_t>(alto)]);
}

}  // namespace

// ------------------------------- SECUENCIAL ----------------------------------
void gaussiano_secuencial(const std::uint8_t* entrada, int ancho, int alto,
                          std::uint8_t* salida) {
    auto T = reservar_intermedia(ancho, alto);
    for (int y = 0; y < alto; ++y) pasada_h_fila(entrada, ancho, y, T.get());
    for (int y = 0; y < alto; ++y)
        pasada_v_fila(T.get(), ancho, alto, y, salida);
}

// -------------------------------- PARALELO -----------------------------------
// Patron: stencil -> se reparte el ciclo de FILAS con "parallel for".
//  * Por filas y no por columnas: la imagen esta guardada por filas, asi que
//    cada hilo recibe bloques contiguos de memoria (aprovecha las lineas de
//    cache y el prefetcher) y dos hilos casi nunca comparten una linea de cache
//    (solo en la frontera entre sus bloques de filas).
//  * schedule(runtime): la politica se escoge SIN recompilar con la variable
//    OMP_SCHEDULE (por ejemplo "static" o "dynamic,16"). Todas las filas
//    cuestan lo mismo, asi que se espera que static gane; mide y justificalo.
//  * Entre las dos pasadas hay una barrera implicita (el final del primer
//    "parallel for"): la pasada vertical necesita TODA T lista porque lee filas
//    que calcularon otros hilos.
void gaussiano_paralelo(const std::uint8_t* entrada, int ancho, int alto,
                        std::uint8_t* salida) {
    auto T = reservar_intermedia(ancho, alto);
    double* t = T.get();

#pragma omp parallel for schedule(runtime)
    for (int y = 0; y < alto; ++y) pasada_h_fila(entrada, ancho, y, t);

#pragma omp parallel for schedule(runtime)
    for (int y = 0; y < alto; ++y) pasada_v_fila(t, ancho, alto, y, salida);
}

// ------------------------------ DIRECTO 5x5 ----------------------------------
// 25 multiplicaciones por pixel (k_j * k_i * I). Solo para la comparacion del
// informe (1 hilo): la separable cuesta 10, y esa mejora algoritmica pesa mas
// que lo que aportan los nucleos.
void gaussiano_directo_5x5(const std::uint8_t* entrada, int ancho, int alto,
                           std::uint8_t* salida) {
    for (int y = 0; y < alto; ++y) {
        for (int x = 0; x < ancho; ++x) {
            double s = 0.0;
            for (int j = -2; j <= 2; ++j) {
                const int yy = recortar(y + j, 0, alto - 1);
                for (int i = -2; i <= 2; ++i) {
                    const int xx = recortar(x + i, 0, ancho - 1);
                    s += K[j + 2] * K[i + 2] * entrada[idx(yy, xx, ancho)];
                }
            }
            salida[idx(y, x, ancho)] = a_byte(s);
        }
    }
}

}  // namespace estencil