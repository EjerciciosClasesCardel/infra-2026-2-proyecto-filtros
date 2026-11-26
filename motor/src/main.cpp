#include "filtros.hpp"
#include "imagen.hpp"

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

const char* USO =
    "uso: motor <filtros> <entrada> <salida> [--secuencial]\n"
    "  filtros: lista separada por comas de gris, caja, gaussiano, sobel, otsu\n"
    "  ejemplo: motor gris,gaussiano,sobel,otsu entrada.ppm salida.pgm\n";

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
           filtro == "sobel" || filtro == "otsu"|| filtro == "ecualizar";
}

Imagen aplicar(const std::string& filtro, const Imagen& entrada, bool secuencial) {
    if (filtro == "gris") return secuencial ? gris_secuencial(entrada) : gris_paralelo(entrada);
    if (filtro == "caja") return secuencial ? caja_secuencial(entrada) : caja_paralelo(entrada);
    if (filtro == "gaussiano") {
        return secuencial ? gaussiano_secuencial(entrada) : gaussiano_paralelo(entrada);
    }
    if (filtro == "sobel") return secuencial ? sobel_secuencial(entrada) : sobel_paralelo(entrada);
    if (filtro == "otsu") return secuencial ? otsu_secuencial(entrada) : otsu_paralelo(entrada);
    if (filtro == "ecualizar") return secuencial ? ecualizar_secuencial(entrada) : ecualizar_paralelo(entrada);
    throw std::runtime_error("filtro desconocido: " + filtro);
}

// Devuelve el motivo por el que la cadena no se puede aplicar, o una cadena
// vacía si se puede. El trabajador responde 400 con ese motivo.
std::string cadena_invalida(const std::vector<std::string>& filtros, int canales) {
    for (const std::string& f : filtros) {
        if (!conocido(f)) return "filtro desconocido: " + f;
    }
    // Cada filtro necesita cierto número de canales y entrega otro, así que
    // la cadena se recorre llevando la cuenta: el primero que no reciba lo
    // que necesita es el que se nombra en el rechazo. Un error aquí cuesta
    // una petición; el mismo error a mitad de la cadena cuesta el tiempo de
    // los filtros que ya corrieron.
    int actuales = canales;
    for (const std::string& f : filtros) {
        if (f == "gris") {
            if (actuales != 3) return "gris necesita una imagen de tres canales";
            actuales = 1;
        } else if (f == "sobel" || f == "otsu"|| f == "ecualizar") {
            if (actuales != 1) return f + " necesita una imagen de un canal";
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
        if (std::string(argv[i]) == "--secuencial") secuencial = true;
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
