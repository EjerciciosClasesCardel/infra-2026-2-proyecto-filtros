#include "filtros.hpp"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

// Los filtros gaussiano y Sobel viven en sus propios archivos y trabajan sobre
// buffers crudos de un canal (namespace estencil). Aqui abajo se adaptan a la
// interfaz con Imagen que usan main.cpp y el arnes.
#include "gaussiano.hpp"
#include "sobel.hpp"

Imagen gris_secuencial(const Imagen& e) {
    if (e.canales != 3) throw std::runtime_error("gris espera una imagen de tres canales");
    Imagen s;
    s.ancho = e.ancho;
    s.alto = e.alto;
    s.canales = 1;
    s.datos.resize(static_cast<std::size_t>(e.ancho) * e.alto);
    for (int y = 0; y < e.alto; ++y) {
        for (int x = 0; x < e.ancho; ++x) {
            const double v = 0.299 * e.en(x, y, 0) + 0.587 * e.en(x, y, 1) + 0.114 * e.en(x, y, 2);
            s.poner(x, y, 0, redondear(v));
        }
    }
    return s;
}

Imagen gris_paralelo(const Imagen& e) {
    if (e.canales != 3) throw std::runtime_error("gris espera una imagen de tres canales");
    Imagen s;
    s.ancho = e.ancho;
    s.alto = e.alto;
    s.canales = 1;
    s.datos.resize(static_cast<std::size_t>(e.ancho) * e.alto);
    // Cada fila es independiente de las demás: no hay nada que compartir
    // entre hilos ni dependencias entre iteraciones.
    #pragma omp parallel for schedule(static)
    for (int y = 0; y < e.alto; ++y) {
        for (int x = 0; x < e.ancho; ++x) {
            const double v = 0.299 * e.en(x, y, 0) + 0.587 * e.en(x, y, 1) + 0.114 * e.en(x, y, 2);
            s.poner(x, y, 0, redondear(v));
        }
    }
    return s;
}

namespace {

// El promedio de la vecindad de 3x3 de un canal, con las coordenadas
// recortadas al rectángulo de la imagen.
double promedio_vecindad(const Imagen& e, int x, int y, int c) {
    double suma = 0.0;
    for (int j = -1; j <= 1; ++j) {
        const int fila = recortar(y + j, e.alto);
        for (int i = -1; i <= 1; ++i) {
            suma += e.en(recortar(x + i, e.ancho), fila, c);
        }
    }
    return suma / 9.0;
}

}  // namespace

Imagen caja_secuencial(const Imagen& e) {
    Imagen s = e;
    for (int y = 0; y < e.alto; ++y) {
        for (int x = 0; x < e.ancho; ++x) {
            for (int c = 0; c < e.canales; ++c) {
                s.poner(x, y, c, redondear(promedio_vecindad(e, x, y, c)));
            }
        }
    }
    return s;
}

Imagen caja_paralelo(const Imagen& e) {
    Imagen s = e;
    // El ciclo externo recorre filas y el interno columnas, que es el orden
    // en que la imagen está en memoria. Repartir por filas deja a cada hilo
    // con una banda contigua.
    #pragma omp parallel for schedule(static)
    for (int y = 0; y < e.alto; ++y) {
        for (int x = 0; x < e.ancho; ++x) {
            for (int c = 0; c < e.canales; ++c) {
                s.poner(x, y, c, redondear(promedio_vecindad(e, x, y, c)));
            }
        }
    }
    return s;
}

namespace {

// Firma comun de los filtros de un canal (ver gaussiano.hpp y sobel.hpp).
using FiltroPlano = void (*)(const std::uint8_t*, int, int, std::uint8_t*);

// Aplica un filtro de UN canal a una Imagen de uno o tres canales. Con un canal
// se llama directo sobre el buffer. Con tres (color) se separa cada canal en
// un plano, se filtra por separado y se vuelve a intercalar R G B, igual que
// hace caja_* (que tambien trata cada canal por su lado).
Imagen aplicar_por_canal(const Imagen& e, FiltroPlano f) {
    if (e.canales != 1 && e.canales != 3) {
        throw std::runtime_error("el filtro espera una imagen de uno o tres canales");
    }
    Imagen s;
    s.ancho = e.ancho;
    s.alto = e.alto;
    s.canales = e.canales;
    s.datos.resize(e.datos.size());

    if (e.canales == 1) {
        f(e.datos.data(), e.ancho, e.alto, s.datos.data());
        return s;
    }

    const std::size_t n = static_cast<std::size_t>(e.ancho) * e.alto;
    std::vector<std::uint8_t> plano_in(n), plano_out(n);
    for (int c = 0; c < 3; ++c) {
        for (std::size_t i = 0; i < n; ++i) plano_in[i] = e.datos[i * 3 + c];
        f(plano_in.data(), e.ancho, e.alto, plano_out.data());
        for (std::size_t i = 0; i < n; ++i) s.datos[i * 3 + c] = plano_out[i];
    }
    return s;
}

}  // namespace

// Gaussiano separable 5x5 (dos pasadas). Ver gaussiano.cpp.
Imagen gaussiano_secuencial(const Imagen& e) {
    return aplicar_por_canal(e, estencil::gaussiano_secuencial);
}

Imagen gaussiano_paralelo(const Imagen& e) {
    return aplicar_por_canal(e, estencil::gaussiano_paralelo);
}

// Forma directa 5x5, solo para la comparacion de tiempos del informe.
Imagen gaussiano_directo(const Imagen& e) {
    return aplicar_por_canal(e, estencil::gaussiano_directo_5x5);
}

// Sobel solo tiene sentido sobre grises: main.cpp ya rechaza la cadena si le
// llega color, y aqui se protege tambien quien llame al filtro directamente.
Imagen sobel_secuencial(const Imagen& e) {
    if (e.canales != 1) throw std::runtime_error("sobel espera una imagen de un canal");
    return aplicar_por_canal(e, estencil::sobel_secuencial);
}

Imagen sobel_paralelo(const Imagen& e) {
    if (e.canales != 1) throw std::runtime_error("sobel espera una imagen de un canal");
    return aplicar_por_canal(e, estencil::sobel_paralelo);
}

Imagen otsu_secuencial(const Imagen&) {
    throw std::runtime_error("otsu: sin implementar");
}

Imagen otsu_paralelo(const Imagen&) {
    throw std::runtime_error("otsu: sin implementar");
}