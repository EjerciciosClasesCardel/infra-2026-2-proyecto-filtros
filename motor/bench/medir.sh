
#!/usr/bin/env bash
# Mediciones secuenciales y paralelas del motor.
# Uso: bash bench/medir.sh imagen_color.ppm

set -euo pipefail

if [ $# -ne 1 ]; then
    echo "uso: bash bench/medir.sh <imagen_color.ppm>" >&2
    exit 2
fi

color="$1"
salida="../mediciones/motor.csv"
repeticiones=9

if [ ! -f "$color" ]; then
    echo "no existe la imagen: $color" >&2
    exit 1
fi

mkdir -p ../mediciones

gris="$(mktemp --suffix=.pgm)"
trap 'rm -f "$gris"' EXIT

# Crear la versión en escala de grises.
./motor gris "$color" "$gris"

# Encabezado del CSV.
echo "filtro,hilos,repeticion,milisegundos" > "$salida"

# Medir una versión secuencial y renombrarla en el CSV.
medir_secuencial() {
    local filtro="$1"
    local imagen="$2"

    ./arnes "$filtro" "$imagen" "$repeticiones" --secuencial |
        sed "s/^${filtro},/${filtro}_secuencial,/" >> "$salida"
}

# Medir la versión paralela con 1, 2, 4 y 8 hilos.
medir_paralelo() {
    local filtro="$1"
    local imagen="$2"

    for hilos in 1 2 4 8; do
        OMP_NUM_THREADS="$hilos" \
            ./arnes "$filtro" "$imagen" "$repeticiones" >> "$salida"
    done
}

echo "Midiendo escala de grises..."
medir_secuencial gris "$color"
medir_paralelo gris "$color"

echo "Midiendo caja..."
medir_secuencial caja "$gris"
medir_paralelo caja "$gris"

echo "Midiendo gaussiano..."
medir_secuencial gaussiano "$gris"
medir_paralelo gaussiano "$gris"

echo "Midiendo gaussiano de dos pasadas..."
medir_secuencial gaussiano2p "$gris"
medir_paralelo gaussiano2p "$gris"

echo "Midiendo gaussiano directo 5x5..."
medir_secuencial gaussiano5x5 "$gris"
medir_paralelo gaussiano5x5 "$gris"

echo "Midiendo Sobel..."
medir_secuencial sobel "$gris"
medir_paralelo sobel "$gris"

echo "Midiendo ecualización..."
medir_secuencial ecualizar "$gris"
medir_paralelo ecualizar "$gris"

echo "Midiendo Otsu..."
medir_secuencial otsu "$gris"
medir_paralelo otsu "$gris"

echo "Midiendo histogramas..."
./arnes histograma "$gris" "$repeticiones" >> "$salida"
medir_paralelo histograma_compartido "$gris"
medir_paralelo histograma_privado "$gris"

echo ""
echo "Mediciones terminadas."
echo "Archivo: $salida"
echo "Filas registradas: $(( $(wc -l < "$salida") - 1 ))"


