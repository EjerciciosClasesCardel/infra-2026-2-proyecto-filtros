// gaussiano.hpp
// -----------------------------------------------------------------------------
// Desenfoque gaussiano 5x5 en tres versiones:
//   1. gaussiano_secuencial  : forma SEPARABLE (dos pasadas de 1D), un hilo.
//   2. gaussiano_paralelo    : la misma forma separable, repartida con OpenMP.
//   3. gaussiano_directo_5x5 : forma directa en 2D (25 multiplicaciones por
//                              pixel). SOLO sirve para comparar tiempos con un
//                              hilo; no es el filtro final de la cadena.
//
// Convenciones fijas del enunciado (seccion 2.8):
//   - Bordes: repeticion del pixel mas cercano (se recortan las coordenadas).
//   - Aritmetica en double y redondeo SOLO al final (empates hacia arriba),
//     saturando a [0, 255].
//
// Interfaz: punteros crudos a imagenes de UN canal (escala de grises, PGM)
// almacenadas por filas. Asi no depende de como este hecha la clase Imagen
// del repositorio: basta pasar img.datos.data() (o como se llame el buffer).
// La memoria de entrada y salida la reserva quien llama (el arnes, la prueba).
// -----------------------------------------------------------------------------
#ifndef GAUSSIANO_HPP
#define GAUSSIANO_HPP

#include <cstdint>

namespace estencil {

// Separable, un solo hilo. 'salida' debe tener ancho*alto bytes.
void gaussiano_secuencial(const std::uint8_t* entrada, int ancho, int alto,
                          std::uint8_t* salida);

// Separable con OpenMP. Produce EXACTAMENTE los mismos bytes que la secuencial
// con cualquier numero de hilos (ver el comentario en gaussiano.cpp).
void gaussiano_paralelo(const std::uint8_t* entrada, int ancho, int alto,
                        std::uint8_t* salida);

// Forma directa 5x5 (25 multiplicaciones por pixel), un solo hilo.
void gaussiano_directo_5x5(const std::uint8_t* entrada, int ancho, int alto,
                           std::uint8_t* salida);

}  // namespace estencil

#endif  // GAUSSIANO_HPP