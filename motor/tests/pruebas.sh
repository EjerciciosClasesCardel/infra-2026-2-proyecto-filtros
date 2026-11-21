#!/usr/bin/env bash
# Pruebas de corrección del motor. Cada filtro se compara contra la salida
# que el repositorio trae calculada, y después la versión paralela contra la
# secuencial sobre una imagen grande, donde el reparto entre hilos sí tiene
# efecto.
set -uo pipefail

cd "$(dirname "$0")/.."
fallos=0
trabajo=$(mktemp -d)
trap 'rm -rf "$trabajo"' EXIT

revisar() {
    local nombre="$1"
    shift
    if "$@" > "$trabajo/salida.txt" 2>&1; then
        echo "  ok    $nombre"
    else
        echo "  FALLA $nombre"
        sed 's/^/        /' "$trabajo/salida.txt"
        fallos=$((fallos + 1))
    fi
}

echo "Salida esperada, calculada aparte:"
./motor gris tests/datos/entrada.ppm "$trabajo/gris.pgm"
revisar "gris sobre la imagen de prueba" ./comparar "$trabajo/gris.pgm" tests/datos/gris.pgm
./motor caja tests/datos/entrada.ppm "$trabajo/caja.ppm"
revisar "caja sobre la imagen de prueba" ./comparar "$trabajo/caja.ppm" tests/datos/caja.ppm

echo "Lectura y escritura:"
./motor gris tests/datos/gris.pgm "$trabajo/vuelta.pgm" 2> "$trabajo/err.txt"
if grep -q "tres canales" "$trabajo/err.txt"; then
    echo "  ok    gris rechaza una imagen de un canal"
else
    echo "  FALLA gris debería rechazar una imagen de un canal"
    fallos=$((fallos + 1))
fi
./motor caja tests/datos/gris.pgm "$trabajo/caja1.pgm"
revisar "caja sobre un canal escribe P5" bash -c \
    "head -c 2 '$trabajo/caja1.pgm' | grep -q P5"

echo "Paralelo contra secuencial, imagen de 512x512:"
./generar "$trabajo/grande.ppm" 512 512 > /dev/null
for filtro in gris caja; do
    ./motor "$filtro" "$trabajo/grande.ppm" "$trabajo/$filtro-par" 
    ./motor "$filtro" "$trabajo/grande.ppm" "$trabajo/$filtro-sec" --secuencial
    revisar "$filtro idéntico con uno y con varios hilos" \
        ./comparar "$trabajo/$filtro-par" "$trabajo/$filtro-sec"
done

echo "Gaussiano y Sobel contra la referencia:"
for imagen in entrada textura; do
    ./motor gaussiano "tests/datos/$imagen.ppm" "$trabajo/g-$imagen.ppm" --secuencial
done
revisar "gaussiano sobre la imagen de prueba" \
    ./comparar "$trabajo/g-entrada.ppm" tests/datos/gaussiano.ppm
revisar "gaussiano sobre la textura de 37x23" \
    ./comparar "$trabajo/g-textura.ppm" tests/datos/textura_gaussiano.ppm
./motor sobel tests/datos/gris.pgm "$trabajo/s-entrada.pgm" --secuencial
./motor sobel tests/datos/textura_gris.pgm "$trabajo/s-textura.pgm" --secuencial
revisar "sobel sobre la imagen de prueba" \
    ./comparar "$trabajo/s-entrada.pgm" tests/datos/sobel.pgm
revisar "sobel sobre la textura de 37x23" \
    ./comparar "$trabajo/s-textura.pgm" tests/datos/textura_sobel.pgm

echo "Gaussiano y Sobel, paralelo contra secuencial con 1 y 8 hilos:"
./motor gris "$trabajo/grande.ppm" "$trabajo/grande.pgm"
./motor gaussiano "$trabajo/grande.ppm" "$trabajo/gauss-sec.ppm" --secuencial
./motor gaussiano "$trabajo/grande.pgm" "$trabajo/gauss-sec.pgm" --secuencial
./motor sobel "$trabajo/grande.pgm" "$trabajo/sobel-sec.pgm" --secuencial
for hilos in 1 8; do
    for variante in gaussiano gaussiano2p gaussiano5x5; do
        OMP_NUM_THREADS="$hilos" ./motor "$variante" "$trabajo/grande.ppm" "$trabajo/$variante-$hilos.ppm"
        revisar "$variante con $hilos hilo(s), tres canales" \
            ./comparar "$trabajo/$variante-$hilos.ppm" "$trabajo/gauss-sec.ppm"
        OMP_NUM_THREADS="$hilos" ./motor "$variante" "$trabajo/grande.pgm" "$trabajo/$variante-$hilos.pgm"
        revisar "$variante con $hilos hilo(s), un canal" \
            ./comparar "$trabajo/$variante-$hilos.pgm" "$trabajo/gauss-sec.pgm"
    done
    OMP_NUM_THREADS="$hilos" ./motor sobel "$trabajo/grande.pgm" "$trabajo/sobel-$hilos.pgm"
    revisar "sobel con $hilos hilo(s)" \
        ./comparar "$trabajo/sobel-$hilos.pgm" "$trabajo/sobel-sec.pgm"
done
./motor gaussiano5x5 "$trabajo/grande.ppm" "$trabajo/directo-sec.ppm" --secuencial
revisar "la forma directa de 5x5 coincide con la separable" \
    ./comparar "$trabajo/directo-sec.ppm" "$trabajo/gauss-sec.ppm"

echo "Filtro desconocido:"
# El motor sale con código 1 al rechazar la cadena, así que su salida se
# recoge antes de buscar en ella: con pipefail, la tubería heredaría ese
# código y la prueba fallaría por el motivo contrario al que busca.
queja=$(./motor nosexiste tests/datos/entrada.ppm "$trabajo/x" 2>&1 || true)
if printf '%s' "$queja" | grep -q "desconocido"; then
    echo "  ok    la cadena con un filtro inexistente se rechaza"
else
    echo "  FALLA una cadena con un filtro inexistente debería rechazarse"
    fallos=$((fallos + 1))
fi

echo "Cadenas que no se pueden aplicar:"
for caso in "sobel:un canal" "gris,gris:tres canales" "otsu:un canal"; do
    cadena="${caso%%:*}"
    espera="${caso##*:}"
    queja=$(./motor "$cadena" tests/datos/entrada.ppm "$trabajo/x" 2>&1 || true)
    if printf '%s' "$queja" | grep -q "$espera"; then
        echo "  ok    $cadena se rechaza antes de aplicar nada"
    else
        echo "  FALLA $cadena debería rechazarse: $queja"
        fallos=$((fallos + 1))
    fi
done

echo "Histogramas, ecualización y Otsu:"
if ./probar_histograma > "$trabajo/histograma.txt" 2>&1; then
    cat "$trabajo/histograma.txt"
else
    cat "$trabajo/histograma.txt"
    fallos=$((fallos + 1))
fi

echo
if [ "$fallos" -eq 0 ]; then
    echo "todas las pruebas pasaron"
else
    echo "$fallos prueba(s) fallaron"
fi
exit "$fallos"


