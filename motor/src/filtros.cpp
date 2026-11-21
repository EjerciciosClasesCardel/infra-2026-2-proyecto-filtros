
#include "filtros.hpp"

#include <cmath>
#include <cstdint>
#include <omp.h>
#include <stdexcept>
#include <vector>

// ==================================================
// ESCALA DE GRISES
// ==================================================

Imagen gris_secuencial(const Imagen& e) {
    if (e.canales != 3) {
        throw std::runtime_error("gris espera una imagen de tres canales");
    }

    Imagen s;
    s.ancho = e.ancho;
    s.alto = e.alto;
    s.canales = 1;
    s.datos.resize(static_cast<std::size_t>(e.ancho) * e.alto);

    for (int y = 0; y < e.alto; ++y) {
        for (int x = 0; x < e.ancho; ++x) {
            const double v = 0.299 * e.en(x, y, 0)
                           + 0.587 * e.en(x, y, 1)
                           + 0.114 * e.en(x, y, 2);
            s.poner(x, y, 0, redondear(v));
        }
    }

    return s;
}

Imagen gris_paralelo(const Imagen& e) {
    if (e.canales != 3) {
        throw std::runtime_error("gris espera una imagen de tres canales");
    }

    Imagen s;
    s.ancho = e.ancho;
    s.alto = e.alto;
    s.canales = 1;
    s.datos.resize(static_cast<std::size_t>(e.ancho) * e.alto);

    #pragma omp parallel for schedule(static)
    for (int y = 0; y < e.alto; ++y) {
        for (int x = 0; x < e.ancho; ++x) {
            const double v = 0.299 * e.en(x, y, 0)
                           + 0.587 * e.en(x, y, 1)
                           + 0.114 * e.en(x, y, 2);
            s.poner(x, y, 0, redondear(v));
        }
    }

    return s;
}

// ==================================================
// FUNCIONES AUXILIARES
// ==================================================

namespace {

// El promedio de la vecindad de 3x3 de un canal.
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

// ==================================================
// FILTRO DE CAJA
// ==================================================

Imagen caja_secuencial(const Imagen& e) {
    Imagen s = e;

    for (int y = 0; y < e.alto; ++y) {
        for (int x = 0; x < e.ancho; ++x) {
            for (int c = 0; c < e.canales; ++c) {
                s.poner(x, y, c,
                    redondear(promedio_vecindad(e, x, y, c)));
            }
        }
    }

    return s;
}

Imagen caja_paralelo(const Imagen& e) {
    Imagen s = e;

    #pragma omp parallel for schedule(static)
    for (int y = 0; y < e.alto; ++y) {
        for (int x = 0; x < e.ancho; ++x) {
            for (int c = 0; c < e.canales; ++c) {
                s.poner(x, y, c,
                    redondear(promedio_vecindad(e, x, y, c)));
            }
        }
    }

    return s;
}

// ==================================================
// FUNCIONES AUXILIARES PARA GAUSSIANO Y SOBEL
// ==================================================

namespace {

const double NUCLEO[5] = {
    1.0 / 16.0,
    4.0 / 16.0,
    6.0 / 16.0,
    4.0 / 16.0,
    1.0 / 16.0
};

Imagen vacia_como(const Imagen& e, int canales) {
    Imagen s;
    s.ancho = e.ancho;
    s.alto = e.alto;
    s.canales = canales;
    s.datos.resize(
        static_cast<std::size_t>(e.ancho) * e.alto * canales
    );

    return s;
}

const unsigned char* fila_de(const Imagen& e, int y) {
    return e.datos.data()
        + static_cast<std::size_t>(y) * e.ancho * e.canales;
}

void horizontal(
    const unsigned char* fila,
    int ancho,
    int canales,
    double* t
) {
    for (int x = 0; x < ancho; ++x) {
        const bool interior = x >= 2 && x + 2 < ancho;

        for (int c = 0; c < canales; ++c) {
            double suma = 0.0;

            if (interior) {
                const unsigned char* p =
                    fila + (x - 2) * canales + c;

                for (int i = 0; i < 5; ++i) {
                    suma += NUCLEO[i] * p[i * canales];
                }
            } else {
                for (int i = 0; i < 5; ++i) {
                    suma += NUCLEO[i]
                        * fila[
                            recortar(x + i - 2, ancho) * canales + c
                        ];
                }
            }

            t[x * canales + c] = suma;
        }
    }
}

void vertical(
    const double* const filas[5],
    int n,
    unsigned char* salida
) {
    for (int i = 0; i < n; ++i) {
        double suma = 0.0;

        for (int j = 0; j < 5; ++j) {
            suma += NUCLEO[j] * filas[j][i];
        }

        salida[i] = redondear(suma);
    }
}

int ranura(int r) {
    return (r + 5) % 5;
}

void gaussiano5x5_fila(
    const Imagen& e,
    int y,
    unsigned char* salida
) {
    const int ancho = e.ancho;
    const int canales = e.canales;

    const unsigned char* filas[5];

    for (int j = 0; j < 5; ++j) {
        filas[j] = fila_de(e, recortar(y + j - 2, e.alto));
    }

    for (int x = 0; x < ancho; ++x) {
        int columnas[5];

        for (int i = 0; i < 5; ++i) {
            columnas[i] = recortar(x + i - 2, ancho) * canales;
        }

        for (int c = 0; c < canales; ++c) {
            double suma = 0.0;

            for (int j = 0; j < 5; ++j) {
                for (int i = 0; i < 5; ++i) {
                    suma += NUCLEO[j] * NUCLEO[i]
                        * filas[j][columnas[i] + c];
                }
            }

            salida[x * canales + c] = redondear(suma);
        }
    }
}

void sobel_fila(
    const Imagen& e,
    int y,
    unsigned char* salida
) {
    const int ancho = e.ancho;

    const unsigned char* a =
        fila_de(e, recortar(y - 1, e.alto));
    const unsigned char* b = fila_de(e, y);
    const unsigned char* c =
        fila_de(e, recortar(y + 1, e.alto));

    for (int x = 0; x < ancho; ++x) {
        const int iz = recortar(x - 1, ancho);
        const int de = recortar(x + 1, ancho);

        const int gx =
            (a[de] + 2 * b[de] + c[de])
            - (a[iz] + 2 * b[iz] + c[iz]);

        const int gy =
            (c[iz] + 2 * c[x] + c[de])
            - (a[iz] + 2 * a[x] + a[de]);

        salida[x] = redondear(
            std::sqrt(
                static_cast<double>(gx) * gx
                + static_cast<double>(gy) * gy
            )
        );
    }
}

}  // namespace

// ==================================================
// GAUSSIANO SECUENCIAL
// ==================================================

Imagen gaussiano_secuencial(const Imagen& e) {
    Imagen s = vacia_como(e, e.canales);

    const std::size_t n =
        static_cast<std::size_t>(e.ancho) * e.canales;

    std::vector<double> t(n * e.alto);

    for (int y = 0; y < e.alto; ++y) {
        horizontal(
            fila_de(e, y),
            e.ancho,
            e.canales,
            &t[y * n]
        );
    }

    for (int y = 0; y < e.alto; ++y) {
        const double* filas[5];

        for (int j = 0; j < 5; ++j) {
            filas[j] = &t[recortar(y + j - 2, e.alto) * n];
        }

        vertical(filas, static_cast<int>(n), &s.datos[y * n]);
    }

    return s;
}

// ==================================================
// GAUSSIANO PARALELO EN DOS PASADAS
// ==================================================

Imagen gaussiano_dos_pasadas_paralelo(const Imagen& e) {
    Imagen s = vacia_como(e, e.canales);

    const std::size_t n =
        static_cast<std::size_t>(e.ancho) * e.canales;

    std::vector<double> t(n * e.alto);

    #pragma omp parallel
    {
        #pragma omp for schedule(static)
        for (int y = 0; y < e.alto; ++y) {
            horizontal(
                fila_de(e, y),
                e.ancho,
                e.canales,
                &t[y * n]
            );
        }

        #pragma omp for schedule(static)
        for (int y = 0; y < e.alto; ++y) {
            const double* filas[5];

            for (int j = 0; j < 5; ++j) {
                filas[j] =
                    &t[recortar(y + j - 2, e.alto) * n];
            }

            vertical(filas, static_cast<int>(n), &s.datos[y * n]);
        }
    }

    return s;
}

// ==================================================
// GAUSSIANO PARALELO CON ANILLO
// ==================================================

Imagen gaussiano_paralelo(const Imagen& e) {
    Imagen s = vacia_como(e, e.canales);

    const std::size_t n =
        static_cast<std::size_t>(e.ancho) * e.canales;

    #pragma omp parallel
    {
        std::vector<double> anillo(5 * n);
        int siguiente = -1;

        #pragma omp for schedule(static)
        for (int y = 0; y < e.alto; ++y) {
            if (y != siguiente) {
                for (int r = y - 2; r < y + 2; ++r) {
                    horizontal(
                        fila_de(e, recortar(r, e.alto)),
                        e.ancho,
                        e.canales,
                        &anillo[ranura(r) * n]
                    );
                }
            }

            horizontal(
                fila_de(e, recortar(y + 2, e.alto)),
                e.ancho,
                e.canales,
                &anillo[ranura(y + 2) * n]
            );

            const double* filas[5];

            for (int j = 0; j < 5; ++j) {
                filas[j] =
                    &anillo[ranura(y + j - 2) * n];
            }

            vertical(filas, static_cast<int>(n), &s.datos[y * n]);

            siguiente = y + 1;
        }
    }

    return s;
}

// ==================================================
// GAUSSIANO 5X5 SECUENCIAL
// ==================================================

Imagen gaussiano5x5_secuencial(const Imagen& e) {
    Imagen s = vacia_como(e, e.canales);

    const std::size_t n =
        static_cast<std::size_t>(e.ancho) * e.canales;

    for (int y = 0; y < e.alto; ++y) {
        gaussiano5x5_fila(e, y, &s.datos[y * n]);
    }

    return s;
}

// ==================================================
// GAUSSIANO 5X5 PARALELO
// ==================================================

Imagen gaussiano5x5_paralelo(const Imagen& e) {
    Imagen s = vacia_como(e, e.canales);

    const std::size_t n =
        static_cast<std::size_t>(e.ancho) * e.canales;

    #pragma omp parallel for schedule(static)
    for (int y = 0; y < e.alto; ++y) {
        gaussiano5x5_fila(e, y, &s.datos[y * n]);
    }

    return s;
}

// ==================================================
// SOBEL SECUENCIAL
// ==================================================

Imagen sobel_secuencial(const Imagen& e) {
    if (e.canales != 1) {
        throw std::runtime_error("sobel espera una imagen de un canal");
    }

    Imagen s = vacia_como(e, 1);

    for (int y = 0; y < e.alto; ++y) {
        sobel_fila(
            e,
            y,
            &s.datos[static_cast<std::size_t>(y) * e.ancho]
        );
    }

    return s;
}

// ==================================================
// SOBEL PARALELO
// ==================================================

Imagen sobel_paralelo(const Imagen& e) {
    if (e.canales != 1) {
        throw std::runtime_error("sobel espera una imagen de un canal");
    }

    Imagen s = vacia_como(e, 1);

    #pragma omp parallel for schedule(static)
    for (int y = 0; y < e.alto; ++y) {
        sobel_fila(
            e,
            y,
            &s.datos[static_cast<std::size_t>(y) * e.ancho]
        );
    }

    return s;
}

// ==================================================
// FUNCIONES AUXILIARES PARA HISTOGRAMA Y OTSU
// ==================================================

namespace {

void validar_gris(const Imagen& e) {
    if (e.canales != 1) {
        throw std::runtime_error(
            "La operacion espera una imagen de un canal"
        );
    }
}

int calcular_umbral_otsu(const Histograma& h) {
    std::uint64_t total = 0;
    long double suma_total = 0.0L;

    int niveles_ocupados = 0;
    int unico_nivel = 0;

    for (int i = 0; i < 256; ++i) {
        total += h[i];
        suma_total += static_cast<long double>(i) * h[i];

        if (h[i] > 0) {
            ++niveles_ocupados;
            unico_nivel = i;
        }
    }

    if (niveles_ocupados <= 1) {
        return unico_nivel;
    }

    std::uint64_t peso_fondo = 0;
    long double suma_fondo = 0.0L;
    long double mejor_varianza = -1.0L;
    int mejor_umbral = 0;

    for (int t = 0; t < 256; ++t) {
        peso_fondo += h[t];
        suma_fondo += static_cast<long double>(t) * h[t];

        const std::uint64_t peso_objeto =
            total - peso_fondo;

        if (peso_fondo == 0 || peso_objeto == 0) {
            continue;
        }

        const long double media_fondo =
            suma_fondo / peso_fondo;

        const long double media_objeto =
            (suma_total - suma_fondo) / peso_objeto;

        const long double diferencia =
            media_fondo - media_objeto;

        const long double varianza =
            static_cast<long double>(peso_fondo)
            * peso_objeto
            * diferencia
            * diferencia;

        if (varianza > mejor_varianza) {
            mejor_varianza = varianza;
            mejor_umbral = t;
        }
    }

    return mejor_umbral;
}

std::array<unsigned char, 256> construir_lut(
    const Histograma& h,
    std::uint64_t total
) {
    std::array<unsigned char, 256> lut{};

    std::uint64_t acumulado = 0;
    std::uint64_t cdf_min = 0;

    for (int i = 0; i < 256; ++i) {
        acumulado += h[i];

        if (h[i] > 0) {
            cdf_min = acumulado;
            break;
        }
    }

    if (total == 0 || total == cdf_min) {
        return lut;
    }

    acumulado = 0;

    for (int i = 0; i < 256; ++i) {
        acumulado += h[i];

        if (acumulado <= cdf_min) {
            lut[i] = 0;
        } else {
            const double valor =
                255.0
                * static_cast<double>(acumulado - cdf_min)
                / static_cast<double>(total - cdf_min);

            lut[i] = redondear(valor);
        }
    }

    return lut;
}

}  // namespace

// ==================================================
// HISTOGRAMA SECUENCIAL
// ==================================================

Histograma histograma_secuencial(const Imagen& e) {
    validar_gris(e);

    Histograma h{};

    for (unsigned char pixel : e.datos) {
        ++h[pixel];
    }

    return h;
}

// ==================================================
// HISTOGRAMA PARALELO CON ACUMULADOR COMPARTIDO
// ==================================================

Histograma histograma_paralelo_compartido(const Imagen& e) {
    validar_gris(e);

    Histograma h{};

    #pragma omp parallel for schedule(static)
    for (long long i = 0;
         i < static_cast<long long>(e.datos.size());
         ++i) {

        const unsigned char pixel =
            e.datos[static_cast<std::size_t>(i)];

        #pragma omp atomic update
        h[pixel]++;
    }

    return h;
}

// ==================================================
// HISTOGRAMA PARALELO CON ACUMULADORES PRIVADOS
// ==================================================

Histograma histograma_paralelo_privado(const Imagen& e) {
    validar_gris(e);

    const int cantidad_hilos = omp_get_max_threads();

    std::vector<Histograma> privados(
        static_cast<std::size_t>(cantidad_hilos),
        Histograma{}
    );

    #pragma omp parallel
    {
        const int id = omp_get_thread_num();
        Histograma& local = privados[id];

        #pragma omp for schedule(static)
        for (long long i = 0;
             i < static_cast<long long>(e.datos.size());
             ++i) {

            const unsigned char pixel =
                e.datos[static_cast<std::size_t>(i)];

            ++local[pixel];
        }
    }

    Histograma resultado{};

    for (int hilo = 0; hilo < cantidad_hilos; ++hilo) {
        for (int valor = 0; valor < 256; ++valor) {
            resultado[valor] += privados[hilo][valor];
        }
    }

    return resultado;
}

// ==================================================
// ECUALIZACION SECUENCIAL
// ==================================================

Imagen ecualizar_secuencial(const Imagen& e) {
    validar_gris(e);

    const Histograma h = histograma_secuencial(e);

    const std::uint64_t total = e.datos.size();

    const auto lut = construir_lut(h, total);

    Imagen salida = e;

    if (total == 0) {
        return salida;
    }

    std::uint64_t cdf_min = 0;

    for (int i = 0; i < 256; ++i) {
        if (h[i] > 0) {
            cdf_min = h[i];
            break;
        }
    }

    if (total == cdf_min) {
        return salida;
    }

    for (std::size_t i = 0; i < e.datos.size(); ++i) {
        salida.datos[i] = lut[e.datos[i]];
    }

    return salida;
}

// ==================================================
// ECUALIZACION PARALELA
// ==================================================

Imagen ecualizar_paralelo(const Imagen& e) {
    validar_gris(e);

    const Histograma h = histograma_paralelo_privado(e);

    const std::uint64_t total = e.datos.size();

    const auto lut = construir_lut(h, total);

    Imagen salida = e;

    if (total == 0) {
        return salida;
    }

    std::uint64_t cdf_min = 0;

    for (int i = 0; i < 256; ++i) {
        if (h[i] > 0) {
            cdf_min = h[i];
            break;
        }
    }

    if (total == cdf_min) {
        return salida;
    }

    #pragma omp parallel for schedule(static)
    for (long long i = 0;
         i < static_cast<long long>(e.datos.size());
         ++i) {

        const std::size_t posicion =
            static_cast<std::size_t>(i);

        salida.datos[posicion] = lut[e.datos[posicion]];
    }

    return salida;
}

// ==================================================
// OTSU SECUENCIAL
// ==================================================

Imagen otsu_secuencial(const Imagen& e) {
    validar_gris(e);

    const Histograma h = histograma_secuencial(e);

    const int umbral = calcular_umbral_otsu(h);

    Imagen salida = e;

    for (std::size_t i = 0; i < e.datos.size(); ++i) {
        salida.datos[i] =
            e.datos[i] > umbral ? 255 : 0;
    }

    return salida;
}

// ==================================================
// OTSU PARALELO
// ==================================================

Imagen otsu_paralelo(const Imagen& e) {
    validar_gris(e);

    const Histograma h = histograma_paralelo_privado(e);

    const int umbral = calcular_umbral_otsu(h);

    Imagen salida = e;

    #pragma omp parallel for schedule(static)
    for (long long i = 0;
         i < static_cast<long long>(e.datos.size());
         ++i) {

        const std::size_t posicion =
            static_cast<std::size_t>(i);

        salida.datos[posicion] =
            e.datos[posicion] > umbral ? 255 : 0;
    }

    return salida;
}
