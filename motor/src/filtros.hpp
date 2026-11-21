
#ifndef FILTROS_HPP
#define FILTROS_HPP

#include "imagen.hpp"

#include <array>
#include <cstdint>

// Escala de grises con la ponderación BT.601.
// Entra un PPM de color y sale un PGM de un canal.
Imagen gris_secuencial(const Imagen& e);
Imagen gris_paralelo(const Imagen& e);

// Desenfoque de caja: cada píxel pasa a ser el promedio de los nueve
// de su vecindad de 3x3. Trabaja igual sobre uno o tres canales.
Imagen caja_secuencial(const Imagen& e);
Imagen caja_paralelo(const Imagen& e);

// Desenfoque gaussiano separable con el núcleo binomial [1 4 6 4 1]/16.
Imagen gaussiano_secuencial(const Imagen& e);
Imagen gaussiano_paralelo(const Imagen& e);
Imagen gaussiano_dos_pasadas_paralelo(const Imagen& e);
Imagen gaussiano5x5_secuencial(const Imagen& e);
Imagen gaussiano5x5_paralelo(const Imagen& e);

// Magnitud del gradiente de Sobel sobre una imagen de un canal.
Imagen sobel_secuencial(const Imagen& e);
Imagen sobel_paralelo(const Imagen& e);

// Histograma de intensidades de una imagen de un canal.
using Histograma = std::array<std::uint64_t, 256>;

Histograma histograma_secuencial(const Imagen& e);
Histograma histograma_paralelo_compartido(const Imagen& e);
Histograma histograma_paralelo_privado(const Imagen& e);

// Ecualización del histograma.
Imagen ecualizar_secuencial(const Imagen& e);
Imagen ecualizar_paralelo(const Imagen& e);

// Umbral de Otsu: busca el umbral que maximiza la varianza entre clases
// del histograma y binariza con él.
Imagen otsu_secuencial(const Imagen& e);
Imagen otsu_paralelo(const Imagen& e);

#endif
