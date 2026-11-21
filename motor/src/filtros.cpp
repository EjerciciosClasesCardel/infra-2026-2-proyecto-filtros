#include "filtros.hpp"

#include <cmath>
#include <stdexcept>
#include <vector>

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

const double NUCLEO[5] = {1.0 / 16.0, 4.0 / 16.0, 6.0 / 16.0, 4.0 / 16.0, 1.0 / 16.0};

Imagen vacia_como(const Imagen& e, int canales) {
    Imagen s;
    s.ancho = e.ancho;
    s.alto = e.alto;
    s.canales = canales;
    s.datos.resize(static_cast<std::size_t>(e.ancho) * e.alto * canales);
    return s;
}

const unsigned char* fila_de(const Imagen& e, int y) {
    return e.datos.data() + static_cast<std::size_t>(y) * e.ancho * e.canales;
}

void horizontal(const unsigned char* fila, int ancho, int canales, double* t) {
    for (int x = 0; x < ancho; ++x) {
        const bool interior = x >= 2 && x + 2 < ancho;
        for (int c = 0; c < canales; ++c) {
            double suma = 0.0;
            if (interior) {
                const unsigned char* p = fila + (x - 2) * canales + c;
                for (int i = 0; i < 5; ++i) suma += NUCLEO[i] * p[i * canales];
            } else {
                for (int i = 0; i < 5; ++i) {
                    suma += NUCLEO[i] * fila[recortar(x + i - 2, ancho) * canales + c];
                }
            }
            t[x * canales + c] = suma;
        }
    }
}

void vertical(const double* const filas[5], int n, unsigned char* salida) {
    for (int i = 0; i < n; ++i) {
        double suma = 0.0;
        for (int j = 0; j < 5; ++j) suma += NUCLEO[j] * filas[j][i];
        salida[i] = redondear(suma);
    }
}

int ranura(int r) {
    return (r + 5) % 5;
}

void gaussiano5x5_fila(const Imagen& e, int y, unsigned char* salida) {
    const int ancho = e.ancho;
    const int canales = e.canales;
    const unsigned char* filas[5];
    for (int j = 0; j < 5; ++j) filas[j] = fila_de(e, recortar(y + j - 2, e.alto));
    for (int x = 0; x < ancho; ++x) {
        int columnas[5];
        for (int i = 0; i < 5; ++i) columnas[i] = recortar(x + i - 2, ancho) * canales;
        for (int c = 0; c < canales; ++c) {
            double suma = 0.0;
            for (int j = 0; j < 5; ++j) {
                for (int i = 0; i < 5; ++i) {
                    suma += NUCLEO[j] * NUCLEO[i] * filas[j][columnas[i] + c];
                }
            }
            salida[x * canales + c] = redondear(suma);
        }
    }
}

void sobel_fila(const Imagen& e, int y, unsigned char* salida) {
    const int ancho = e.ancho;
    const unsigned char* a = fila_de(e, recortar(y - 1, e.alto));
    const unsigned char* b = fila_de(e, y);
    const unsigned char* c = fila_de(e, recortar(y + 1, e.alto));
    for (int x = 0; x < ancho; ++x) {
        const int iz = recortar(x - 1, ancho);
        const int de = recortar(x + 1, ancho);
        const int gx = (a[de] + 2 * b[de] + c[de]) - (a[iz] + 2 * b[iz] + c[iz]);
        const int gy = (c[iz] + 2 * c[x] + c[de]) - (a[iz] + 2 * a[x] + a[de]);
        salida[x] = redondear(std::sqrt(static_cast<double>(gx) * gx + static_cast<double>(gy) * gy));
    }
}

}

Imagen gaussiano_secuencial(const Imagen& e) {
    Imagen s = vacia_como(e, e.canales);
    const std::size_t n = static_cast<std::size_t>(e.ancho) * e.canales;
    std::vector<double> t(n * e.alto);
    for (int y = 0; y < e.alto; ++y) {
        horizontal(fila_de(e, y), e.ancho, e.canales, &t[y * n]);
    }
    for (int y = 0; y < e.alto; ++y) {
        const double* filas[5];
        for (int j = 0; j < 5; ++j) filas[j] = &t[recortar(y + j - 2, e.alto) * n];
        vertical(filas, static_cast<int>(n), &s.datos[y * n]);
    }
    return s;
}

Imagen gaussiano_dos_pasadas_paralelo(const Imagen& e) {
    Imagen s = vacia_como(e, e.canales);
    const std::size_t n = static_cast<std::size_t>(e.ancho) * e.canales;
    std::vector<double> t(n * e.alto);
    #pragma omp parallel
    {
        #pragma omp for schedule(static)
        for (int y = 0; y < e.alto; ++y) {
            horizontal(fila_de(e, y), e.ancho, e.canales, &t[y * n]);
        }
        #pragma omp for schedule(static)
        for (int y = 0; y < e.alto; ++y) {
            const double* filas[5];
            for (int j = 0; j < 5; ++j) filas[j] = &t[recortar(y + j - 2, e.alto) * n];
            vertical(filas, static_cast<int>(n), &s.datos[y * n]);
        }
    }
    return s;
}

Imagen gaussiano_paralelo(const Imagen& e) {
    Imagen s = vacia_como(e, e.canales);
    const std::size_t n = static_cast<std::size_t>(e.ancho) * e.canales;
    #pragma omp parallel
    {
        std::vector<double> anillo(5 * n);
        int siguiente = -1;
        #pragma omp for schedule(static)
        for (int y = 0; y < e.alto; ++y) {
            if (y != siguiente) {
                for (int r = y - 2; r < y + 2; ++r) {
                    horizontal(fila_de(e, recortar(r, e.alto)), e.ancho, e.canales,
                               &anillo[ranura(r) * n]);
                }
            }
            horizontal(fila_de(e, recortar(y + 2, e.alto)), e.ancho, e.canales,
                       &anillo[ranura(y + 2) * n]);
            const double* filas[5];
            for (int j = 0; j < 5; ++j) filas[j] = &anillo[ranura(y + j - 2) * n];
            vertical(filas, static_cast<int>(n), &s.datos[y * n]);
            siguiente = y + 1;
        }
    }
    return s;
}

Imagen gaussiano5x5_secuencial(const Imagen& e) {
    Imagen s = vacia_como(e, e.canales);
    const std::size_t n = static_cast<std::size_t>(e.ancho) * e.canales;
    for (int y = 0; y < e.alto; ++y) {
        gaussiano5x5_fila(e, y, &s.datos[y * n]);
    }
    return s;
}

Imagen gaussiano5x5_paralelo(const Imagen& e) {
    Imagen s = vacia_como(e, e.canales);
    const std::size_t n = static_cast<std::size_t>(e.ancho) * e.canales;
    #pragma omp parallel for schedule(static)
    for (int y = 0; y < e.alto; ++y) {
        gaussiano5x5_fila(e, y, &s.datos[y * n]);
    }
    return s;
}

Imagen sobel_secuencial(const Imagen& e) {
    if (e.canales != 1) throw std::runtime_error("sobel espera una imagen de un canal");
    Imagen s = vacia_como(e, 1);
    for (int y = 0; y < e.alto; ++y) {
        sobel_fila(e, y, &s.datos[static_cast<std::size_t>(y) * e.ancho]);
    }
    return s;
}

Imagen sobel_paralelo(const Imagen& e) {
    if (e.canales != 1) throw std::runtime_error("sobel espera una imagen de un canal");
    Imagen s = vacia_como(e, 1);
    #pragma omp parallel for schedule(static)
    for (int y = 0; y < e.alto; ++y) {
        sobel_fila(e, y, &s.datos[static_cast<std::size_t>(y) * e.ancho]);
    }
    return s;
}

Imagen otsu_secuencial(const Imagen&) {
    throw std::runtime_error("otsu: sin implementar");
}

Imagen otsu_paralelo(const Imagen&) {
    throw std::runtime_error("otsu: sin implementar");
}
