#!/usr/bin/env bash
set -euo pipefail

if [ $# -lt 1 ]; then
    echo "uso: bash bench/localidad.sh <imagen.ppm> [salida.csv]" >&2
    exit 2
fi

color="$1"
salida="${2:-../mediciones/localidad.csv}"
repeticiones=5
gris="$(mktemp --suffix=.pgm)"
trap 'rm -f "$gris"' EXIT

./motor gris "$color" "$gris"

echo "filtro,hilos,repeticion,milisegundos" > "$salida"

./arnes gaussiano "$gris" "$repeticiones" --secuencial | sed 's/^gaussiano,/separable_secuencial,/' >> "$salida"
./arnes gaussiano5x5 "$gris" "$repeticiones" --secuencial | sed 's/^gaussiano5x5,/directo5x5_secuencial,/' >> "$salida"

for hilos in 1 2 4 8; do
    for filtro in gaussiano2p gaussiano sobel; do
        OMP_NUM_THREADS="$hilos" ./arnes "$filtro" "$gris" "$repeticiones" >> "$salida"
    done
done

echo "escrito $salida con $(( $(wc -l < "$salida") - 1 )) filas"
