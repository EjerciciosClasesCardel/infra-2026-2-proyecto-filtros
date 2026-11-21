#include "filtros.hpp"
#include "imagen.hpp"

#include <omp.h>

#include <cstdint>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void comprobar(bool condicion, const std::string& mensaje) {
    if (!condicion) {
        throw std::runtime_error(mensaje);
    }
    std::cout << "  ok    " << mensaje << '\n';
}

bool imagenes_iguales(const Imagen& a, const Imagen& b) {
    return a.ancho == b.ancho &&
           a.alto == b.alto &&
           a.canales == b.canales &&
           a.datos == b.datos;
}

void probar_imagen(const std::string& ruta) {
    const Imagen imagen = leer(ruta);

    comprobar(imagen.canales == 1, ruta + ": imagen de un canal");

    const Histograma referencia = histograma_secuencial(imagen);

    const std::uint64_t total = std::accumulate(
        referencia.begin(), referencia.end(), std::uint64_t{0}
    );

    comprobar(
        total == imagen.datos.size(),
        ruta + ": la suma del histograma coincide con los píxeles"
    );

    comprobar(
        histograma_paralelo_compartido(imagen) == referencia,
        ruta + ": histograma paralelo compartido coincide con el secuencial"
    );

    comprobar(
        histograma_paralelo_privado(imagen) == referencia,
        ruta + ": histograma paralelo privado coincide con el secuencial"
    );

    const Imagen ecualizada_secuencial = ecualizar_secuencial(imagen);
    const Imagen otsu_secuencial_resultado = otsu_secuencial(imagen);

    for (int hilos : {1, 8}) {
        omp_set_num_threads(hilos);

        comprobar(
            histograma_paralelo_compartido(imagen) == referencia,
            ruta + ": histograma compartido correcto con " +
                std::to_string(hilos) + " hilo(s)"
        );

        comprobar(
            histograma_paralelo_privado(imagen) == referencia,
            ruta + ": histograma privado correcto con " +
                std::to_string(hilos) + " hilo(s)"
        );

        const Imagen ecualizada_paralela = ecualizar_paralelo(imagen);
        comprobar(
            imagenes_iguales(ecualizada_secuencial, ecualizada_paralela),
            ruta + ": ecualización paralela idéntica con " +
                std::to_string(hilos) + " hilo(s)"
        );

        const Imagen otsu_paralelo_resultado = otsu_paralelo(imagen);
        comprobar(
            imagenes_iguales(otsu_secuencial_resultado, otsu_paralelo_resultado),
            ruta + ": Otsu paralelo idéntico con " +
                std::to_string(hilos) + " hilo(s)"
        );
    }
}

void probar_imagen_constante() {
    Imagen imagen;
    imagen.ancho = 8;
    imagen.alto = 8;
    imagen.canales = 1;
    imagen.datos.assign(64, 120);

    const Histograma histograma = histograma_secuencial(imagen);

    comprobar(
        histograma[120] == 64,
        "imagen constante: todos los píxeles están en la intensidad 120"
    );

    comprobar(
        std::accumulate(
            histograma.begin(), histograma.end(), std::uint64_t{0}
        ) == 64,
        "imagen constante: la suma del histograma es correcta"
    );

    const Imagen ecualizada = ecualizar_secuencial(imagen);

    comprobar(
        imagenes_iguales(imagen, ecualizada),
        "imagen constante: la ecualización conserva la imagen"
    );

    comprobar(
        imagenes_iguales(ecualizada, ecualizar_paralelo(imagen)),
        "imagen constante: ecualización paralela y secuencial coinciden"
    );

    comprobar(
        imagenes_iguales(otsu_secuencial(imagen), otsu_paralelo(imagen)),
        "imagen constante: Otsu paralelo y secuencial coinciden"
    );
}

}  // namespace

int main() {
    try {
        omp_set_dynamic(0);

        std::cout << "Pruebas de histogramas, ecualización y Otsu:\n";

        probar_imagen("tests/datos/gris.pgm");
        probar_imagen("tests/datos/textura_gris.pgm");
        probar_imagen_constante();

        std::cout << "\nTodas las pruebas de histogramas pasaron.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FALLA: " << error.what() << '\n';
        return 1;
    }
}

