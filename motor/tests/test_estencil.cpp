// test_estencil.cpp
// -----------------------------------------------------------------------------
// Pruebas de correccion del gaussiano separable y de Sobel.
//   1. Valores calculados a mano (verifican formulas, borde y redondeo).
//   2. Casos degenerados (1x1, 1xN, Nx1...): no deben fallar ni salirse.
//   3. Separable == directo 5x5 (en double las sumas son exactas).
//   4. Paralelo == secuencial BYTE A BYTE con 1, 2, 4 y 8 hilos.
// Devuelve 0 si todo pasa y 1 si algo falla (asi lo usa make y GitHub Actions).
// -----------------------------------------------------------------------------
#include <omp.h>

#include <cstdint>
#include <cstdio>
#include <vector>

#include "gaussiano.hpp"
#include "sobel.hpp"

using estencil::gaussiano_directo_5x5;
using estencil::gaussiano_paralelo;
using estencil::gaussiano_secuencial;
using estencil::sobel_paralelo;
using estencil::sobel_secuencial;

using Bytes = std::vector<std::uint8_t>;
using Filtro = void (*)(const std::uint8_t*, int, int, std::uint8_t*);

static int fallos = 0;

#define VERIFICAR(cond, msg)                                        \
    do {                                                            \
        if (!(cond)) {                                              \
            std::printf("  FALLO (linea %d): %s\n", __LINE__, msg); \
            ++fallos;                                               \
        }                                                           \
    } while (0)

// Aplica un filtro sobre un vector y devuelve la salida.
static Bytes aplicar(Filtro f, const Bytes& in, int w, int h) {
    Bytes out(in.size());
    f(in.data(), w, h, out.data());
    return out;
}

// Imagen pseudoaleatoria reproducible (LCG): mismas semillas, mismos bytes.
static Bytes imagen_aleatoria(int w, int h, std::uint32_t semilla) {
    Bytes v(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
    std::uint32_t s = semilla;
    for (auto& b : v) {
        s = s * 1664525u + 1013904223u;
        b = static_cast<std::uint8_t>(s >> 24);
    }
    return v;
}

// 1. Valores a mano --------------------------------------------------------
static void prueba_valores_a_mano() {
    std::printf("[1] valores calculados a mano\n");

    // Imagen constante: el nucleo suma 1, asi que el gaussiano no cambia nada
    // (tambien en los bordes, porque se repite el pixel); Sobel da 0.
    Bytes cte(17 * 9, 77);
    Bytes g = aplicar(gaussiano_secuencial, cte, 17, 9);
    Bytes s = aplicar(sobel_secuencial, cte, 17, 9);
    VERIFICAR(g == cte, "gaussiano de imagen constante debe ser la misma imagen");
    VERIFICAR(s == Bytes(cte.size(), 0), "sobel de imagen constante debe ser 0");

    // Impulso de 255 en el centro de una imagen 5x5. El peso de cada pixel es
    // 255 * wa * wb / 256 con w = {6,4,1} segun la distancia. Redondeado:
    //   36 -> 255*36/256 = 35.86     24 -> 23.91     16 -> 15.94
    //    6 ->  5.98                   4 ->  3.98      1 ->  0.996
    Bytes imp(25, 0);
    imp[2 * 5 + 2] = 255;
    const Bytes esperado = {1, 4,  6,  4,  1,   //
                            4, 16, 24, 16, 4,   //
                            6, 24, 36, 24, 6,   //
                            4, 16, 24, 16, 4,   //
                            1, 4,  6,  4,  1};
    VERIFICAR(aplicar(gaussiano_secuencial, imp, 5, 5) == esperado,
              "gaussiano separable: impulso 5x5");
    VERIFICAR(aplicar(gaussiano_directo_5x5, imp, 5, 5) == esperado,
              "gaussiano directo: impulso 5x5");

    // Borde vertical 4x3, columnas [0 0 10 10]. En x=1 y x=2 el gradiente es
    // gx = (1+2+1)*(10-0) = 40; en x=0 y x=3 la repeticion del borde lo anula.
    // gy = 0 porque las tres filas son iguales.
    Bytes borde = {0, 0, 10, 10, 0, 0, 10, 10, 0, 0, 10, 10};
    Bytes esp_s = {0, 40, 40, 0, 0, 40, 40, 0, 0, 40, 40, 0};
    VERIFICAR(aplicar(sobel_secuencial, borde, 4, 3) == esp_s,
              "sobel: borde vertical 4x3");

    // Saturacion: el mismo borde con 255 da gx = 1020, que debe quedar en 255.
    Bytes fuerte = {0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255};
    Bytes sat = aplicar(sobel_secuencial, fuerte, 4, 3);
    VERIFICAR(sat[1] == 255 && sat[2] == 255, "sobel: saturacion a 255");

    // Redondeo con empate hacia arriba (gaussiano). Imagen 1x1: el filtro debe
    // devolver el mismo valor.
    Bytes uno = {200};
    VERIFICAR(aplicar(gaussiano_secuencial, uno, 1, 1) == uno, "gaussiano 1x1");
    VERIFICAR(aplicar(sobel_secuencial, uno, 1, 1) == Bytes{0}, "sobel 1x1");
}

// 2 y 3. Tamanos degenerados + separable == directo --------------------------
static void prueba_tamanos_y_directo() {
    std::printf("[2] tamanos degenerados y separable == directo 5x5\n");
    const int tam[][2] = {{1, 1}, {1, 7}, {7, 1}, {2, 2}, {3, 3}, {5, 5}, {64, 33}};
    for (const auto& t : tam) {
        const int w = t[0], h = t[1];
        Bytes in = imagen_aleatoria(w, h, 12345u + static_cast<unsigned>(w * 31 + h));
        VERIFICAR(aplicar(gaussiano_secuencial, in, w, h) ==
                      aplicar(gaussiano_directo_5x5, in, w, h),
                  "separable debe coincidir con directo 5x5");
    }
}

// 4. Paralelo == secuencial con varios hilos --------------------------------
static void prueba_paralelo_identico() {
    std::printf("[3] paralelo identico a secuencial (1, 2, 4 y 8 hilos)\n");
    // Dimensiones impares y no multiplos de nada: stress para el reparto.
    const int w = 257, h = 131;
    Bytes in = imagen_aleatoria(w, h, 2026u);
    const Bytes g_seq = aplicar(gaussiano_secuencial, in, w, h);
    const Bytes s_seq = aplicar(sobel_secuencial, in, w, h);

    for (int p : {1, 2, 4, 8}) {
        omp_set_num_threads(p);
        VERIFICAR(aplicar(gaussiano_paralelo, in, w, h) == g_seq,
                  "gaussiano paralelo != secuencial");
        VERIFICAR(aplicar(sobel_paralelo, in, w, h) == s_seq,
                  "sobel paralelo != secuencial");
    }
}

int main() {
    prueba_valores_a_mano();
    prueba_tamanos_y_directo();
    prueba_paralelo_identico();

    if (fallos == 0) {
        std::printf("\nTODAS LAS PRUEBAS PASARON\n");
        return 0;
    }
    std::printf("\n%d PRUEBA(S) FALLARON\n", fallos);
    return 1;
}