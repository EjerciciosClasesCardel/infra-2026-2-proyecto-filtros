// sobel.cpp
// -----------------------------------------------------------------------------
// Sobel con gx y gy calculados en UN SOLO recorrido de la vecindad 3x3.
// Mismo truco que en gaussiano.cpp: una funcion calcula una fila completa y
// la usan tanto la version secuencial como la paralela, asi la salida es
// identica byte a byte.
// -----------------------------------------------------------------------------
#include "sobel.hpp"

#include <cmath>
#include <cstddef>

namespace estencil {
namespace {

inline int recortar(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

inline std::size_t idx(int y, int x, int ancho) {
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(ancho) +
           static_cast<std::size_t>(x);
}

// Redondeo al mas cercano (empates hacia arriba) y saturacion a [0,255].
// La saturacion superior es el "min(255, ...)" del enunciado.
inline std::uint8_t a_byte(double v) {
    double r = std::floor(v + 0.5);
    if (r < 0.0) r = 0.0;
    if (r > 255.0) r = 255.0;
    return static_cast<std::uint8_t>(r);
}

// Calcula la fila y de la salida.
//
// Vecindad (nombres por puntos cardinales):
//        no  n  ne          arriba  (fila y-1)
//        o   .  e           centro  (fila y)   <- el pixel central no pesa
//        so  s  se          abajo   (fila y+1)
//
//   gx = (ne + 2e + se) - (no + 2o + so)     derivada horizontal
//   gy = (so + 2s + se) - (no + 2n + ne)     derivada vertical
//
// Las 8 lecturas se hacen UNA vez y alimentan gx y gy a la vez: hacer dos
// pasadas (una por mascara) leeria la vecindad dos veces.
void sobel_fila(const std::uint8_t* entrada, int ancho, int alto, int y,
                std::uint8_t* salida) {
    // Filas vecinas con el recorte de borde ya aplicado (arriba de la fila 0
    // se repite la fila 0; debajo de la ultima se repite la ultima).
    const std::uint8_t* arriba = entrada + idx(recortar(y - 1, 0, alto - 1), 0, ancho);
    const std::uint8_t* centro = entrada + idx(y, 0, ancho);
    const std::uint8_t* abajo = entrada + idx(recortar(y + 1, 0, alto - 1), 0, ancho);
    std::uint8_t* o_fila = salida + idx(y, 0, ancho);

    for (int x = 0; x < ancho; ++x) {
        const int xm = recortar(x - 1, 0, ancho - 1);  // columna izquierda
        const int xp = recortar(x + 1, 0, ancho - 1);  // columna derecha

        const int no = arriba[xm], n = arriba[x], ne = arriba[xp];
        const int o = centro[xm], e = centro[xp];
        const int so = abajo[xm], s = abajo[x], se = abajo[xp];

        // Enteros pequenos: estas sumas son exactas.
        const double gx = static_cast<double>((ne + 2 * e + se) - (no + 2 * o + so));
        const double gy = static_cast<double>((so + 2 * s + se) - (no + 2 * n + ne));

        // sqrt esta correctamente redondeada por IEEE 754, asi que es
        // determinista: mismo resultado en secuencial y en paralelo.
        o_fila[x] = a_byte(std::sqrt(gx * gx + gy * gy));
    }
}

}  // namespace

void sobel_secuencial(const std::uint8_t* entrada, int ancho, int alto,
                      std::uint8_t* salida) {
    for (int y = 0; y < alto; ++y) sobel_fila(entrada, ancho, alto, y, salida);
}

// Patron: stencil de una sola pasada. Las filas son independientes (leen la
// ENTRADA, nunca la salida), asi que basta repartir el ciclo de filas. No hace
// falta barrera ni sincronizacion. schedule(runtime) permite probar static vs
// dynamic con la variable OMP_SCHEDULE sin recompilar.
void sobel_paralelo(const std::uint8_t* entrada, int ancho, int alto,
                    std::uint8_t* salida) {
#pragma omp parallel for schedule(runtime)
    for (int y = 0; y < alto; ++y) sobel_fila(entrada, ancho, alto, y, salida);
}

}  // namespace estencil