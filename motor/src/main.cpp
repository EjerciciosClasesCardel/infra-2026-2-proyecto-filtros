
#include "filtros.hpp"
#include "imagen.hpp"

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

const char* USO =
    "uso: motor <filtros> <entrada> <salida> [--secuencial]\n"
    "  filtros: lista separada por comas de gris, caja, gaussiano, gaussiano2p,\n"
    "           gaussiano5x5, sobel, ecualizar, otsu\n"
    "  ejemplo: motor gris,ecualizar,gaussiano,sobel,otsu entrada.ppm salida.pgm\n";

std::vector<std::string> partir(const std::string& lista) {
    std::vector<std::string> salida;
    std::istringstream flujo(lista);
    std::string pieza;
    while (std::getline(flujo, pieza, ',')) {
        if (!pieza.empty()) salida.push_back(pieza);
    }
    return salida;
}

bool conocido(const std::string& filtro) {
    return filtro == "gris" || filtro == "caja" || filtro == "gaussiano" ||
           filtro == "gaussiano2p" || filtro == "gaussiano5x5" ||
           filtro == "sobel" || filtro == "ecualizar" || filtro == "otsu";
}

Imagen aplicar(const std::string& filtro, const Imagen& entrada, bool secuencial) {
    if (filtro == "gris") {
        return secuencial ? gris_secuencial(entrada) : gris_paralelo(entrada);
    }
    if (filtro == "caja") {
        return secuencial ? caja_secuencial(entrada) : caja_paralelo(entrada);
    }
    if (filtro == "gaussiano") {
        return secuencial ? gaussiano_secuencial(entrada) : gaussiano_paralelo(entrada);
    }
    if (filtro == "gaussiano2p") {
        return secuencial ? gaussiano_secuencial(entrada)
                          : gaussiano_dos_pasadas_paralelo(entrada);
    }
    if (filtro == "gaussiano5x5") {
        return secuencial ? gaussiano5x5_secuencial(entrada)
                          : gaussiano5x5_paralelo(entrada);
    }
    if (filtro == "sobel") {
        return secuencial ? sobel_secuencial(entrada) : sobel_paralelo(entrada);
    }
    if (filtro == "ecualizar") {
        return secuencial ? ecualizar_secuencial(entrada)
                          : ecualizar_paralelo(entrada);
    }
    if (filtro == "otsu") {
        return secuencial ? otsu_secuencial(entrada) : otsu_paralelo(entrada);
    }
    throw std::runtime_error("filtro desconocido: " + filtro);
}

// Devuelve el motivo por el que la cadena no se puede aplicar, o una cadena
// vacía si se puede. El trabajador responde 400 con ese motivo.
std::string cadena_invalida(const std::vector<std::string>& filtros, int canales) {
    for (const std::string& f : filtros) {
        if (!conocido(f)) return "filtro desconocido: " + f;
    }

    // Cada filtro necesita cierto número de canales y entrega otro, así que
    // la cadena se recorre llevando la cuenta.
    int actuales = canales;
    for (const std::string& f : filtros) {
        if (f == "gris") {
            if (actuales != 3) {
                return "gris necesita una imagen de tres canales";
            }
            actuales = 1;
        } else if (f == "sobel" || f == "ecualizar" || f == "otsu") {
            if (actuales != 1) {
                return f + " necesita una imagen de un canal";
            }
        }
    }
    return "";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << USO;
        return 2;
    }

    const std::vector<std::string> filtros = partir(argv[1]);
    const std::string entrada = argv[2];
    const std::string salida = argv[3];

    bool secuencial = false;
    for (int i = 4; i < argc; ++i) {
        if (std::string(argv[i]) == "--secuencial") {
            secuencial = true;
        }
    }

    if (filtros.empty()) {
        std::cerr << USO;
        return 2;
    }

    try {
        Imagen img = leer(entrada);
        const std::string motivo = cadena_invalida(filtros, img.canales);

        if (!motivo.empty()) {
            std::cerr << "motor: " << motivo << "\n";
            return 1;
        }

        for (const std::string& filtro : filtros) {
            img = aplicar(filtro, img, secuencial);
        }

        escribir(salida, img);
    } catch (const std::exception& error) {
        std::cerr << "motor: " << error.what() << "\n";
        return 1;
    }

    return 0;
}
