

#include "filtros.hpp"
#include "imagen.hpp"

#include <omp.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

Imagen aplicar(const std::string& filtro, const Imagen& entrada,
               bool secuencial) {
    if (filtro == "gris")
        return secuencial ? gris_secuencial(entrada) : gris_paralelo(entrada);

    if (filtro == "caja")
        return secuencial ? caja_secuencial(entrada) : caja_paralelo(entrada);

    if (filtro == "gaussiano") {
        return secuencial ? gaussiano_secuencial(entrada)
                          : gaussiano_paralelo(entrada);
    }

    if (filtro == "gaussiano2p") {
        return secuencial ? gaussiano_secuencial(entrada)
                          : gaussiano_dos_pasadas_paralelo(entrada);
    }

    if (filtro == "gaussiano5x5") {
        return secuencial ? gaussiano5x5_secuencial(entrada)
                          : gaussiano5x5_paralelo(entrada);
    }

    if (filtro == "sobel")
        return secuencial ? sobel_secuencial(entrada)
                          : sobel_paralelo(entrada);

    if (filtro == "ecualizar")
        return secuencial ? ecualizar_secuencial(entrada)
                          : ecualizar_paralelo(entrada);

    if (filtro == "otsu")
        return secuencial ? otsu_secuencial(entrada)
                          : otsu_paralelo(entrada);

    throw std::runtime_error("filtro desconocido: " + filtro);
}

bool es_histograma(const std::string& filtro) {
    return filtro == "histograma" ||
           filtro == "histograma_compartido" ||
           filtro == "histograma_privado";
}

Histograma calcular_histograma(const std::string& filtro,
                               const Imagen& entrada) {
    if (filtro == "histograma")
        return histograma_secuencial(entrada);

    if (filtro == "histograma_compartido")
        return histograma_paralelo_compartido(entrada);

    if (filtro == "histograma_privado")
        return histograma_paralelo_privado(entrada);

    throw std::runtime_error("histograma desconocido: " + filtro);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr
            << "uso: arnes <filtro> <imagen> [repeticiones] "
            << "[--secuencial] [--encabezado]\n";
        return 2;
    }

    const std::string filtro = argv[1];
    const std::string ruta = argv[2];

    int repeticiones = 5;
    bool secuencial = false;
    bool encabezado = false;

    for (int i = 3; i < argc; ++i) {
        const std::string arg = argv[i];

        if (arg == "--secuencial") {
            secuencial = true;
        } else if (arg == "--encabezado") {
            encabezado = true;
        } else {
            repeticiones = std::atoi(arg.c_str());
        }
    }

    if (repeticiones < 1) {
        std::cerr << "arnes: repeticiones tiene que ser al menos 1\n";
        return 2;
    }

    try {
        const Imagen entrada = leer(ruta);

        if (es_histograma(filtro) && entrada.canales != 1) {
            throw std::runtime_error(
                "los histogramas requieren una imagen de un canal"
            );
        }

        const int hilos =
            (secuencial || filtro == "histograma") ? 1 : omp_get_max_threads();

        if (encabezado) {
            std::cout << "filtro,hilos,repeticion,milisegundos\n";
        }

        std::vector<double> medidas;

        for (int r = 1; r <= repeticiones; ++r) {
            std::uint64_t comprobacion = 0;

            const double inicio = omp_get_wtime();

            if (es_histograma(filtro)) {
                const Histograma resultado =
                    calcular_histograma(filtro, entrada);

                const double ms = (omp_get_wtime() - inicio) * 1000.0;

                comprobacion = std::accumulate(
                    resultado.begin(), resultado.end(), std::uint64_t{0}
                );

                if (comprobacion == 0) {
                    throw std::runtime_error(
                        "el histograma devolvió cero píxeles"
                    );
                }

                medidas.push_back(ms);
                std::cout << filtro << "," << hilos << "," << r
                          << "," << ms << "\n";
            } else {
                const Imagen salida = aplicar(filtro, entrada, secuencial);

                const double ms = (omp_get_wtime() - inicio) * 1000.0;

                if (salida.datos.empty()) {
                    throw std::runtime_error(
                        "el filtro devolvió una imagen vacía"
                    );
                }

                comprobacion = std::accumulate(
                    salida.datos.begin(), salida.datos.end(),
                    std::uint64_t{0}
                );

                medidas.push_back(ms);
                std::cout << filtro << "," << hilos << "," << r
                          << "," << ms << "\n";
            }

            if (comprobacion == 0) {
                throw std::runtime_error("resultado de comprobación vacío");
            }
        }

        if (medidas.size() > 1) {
            std::vector<double> utiles(
                medidas.begin() + 1, medidas.end()
            );

            std::sort(utiles.begin(), utiles.end());

            const std::size_t mitad = utiles.size() / 2;

            const double mediana =
                utiles.size() % 2 == 1
                    ? utiles[mitad]
                    : (utiles[mitad - 1] + utiles[mitad]) / 2.0;

            std::cerr << filtro << " con " << hilos
                      << (hilos == 1 ? " hilo" : " hilos")
                      << ": mediana " << mediana
                      << " ms sobre " << utiles.size()
                      << " corridas\n";
        }

    } catch (const std::exception& error) {
        std::cerr << "arnes: " << error.what() << "\n";
        return 1;
    }

    return 0;
}

