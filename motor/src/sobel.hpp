// sobel.hpp
// -----------------------------------------------------------------------------
// Filtro de Sobel: magnitud del gradiente saturada a 255.
//
//   Gx = [-1 0 1; -2 0 2; -1 0 1]     Gy = [-1 -2 -1; 0 0 0; 1 2 1]
//   O(x,y) = min(255, redondeo( sqrt(gx^2 + gy^2) ))
//
// Entrada: imagen en grises (1 canal). Salida: 1 canal.
// Convenciones: borde por repeticion del pixel mas cercano, double y redondeo
// al final con empates hacia arriba.
// -----------------------------------------------------------------------------
#ifndef SOBEL_HPP
#define SOBEL_HPP

#include <cstdint>

namespace estencil {

// Un solo hilo. 'salida' debe tener ancho*alto bytes.
void sobel_secuencial(const std::uint8_t* entrada, int ancho, int alto,
                      std::uint8_t* salida);

// OpenMP. Identico byte a byte a la secuencial con cualquier numero de hilos.
void sobel_paralelo(const std::uint8_t* entrada, int ancho, int alto,
                    std::uint8_t* salida);

}  // namespace estencil

#endif  // SOBEL_HPP