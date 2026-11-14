#!/usr/bin/env bash
# Levanta el servicio completo y ejecuta el lote con el número de trabajadores
# que se pida. Al terminar el despachador, Compose baja los trabajadores.
#
#   bash deploy/local/correr.sh 1
#   bash deploy/local/correr.sh 4
set -euo pipefail

aqui="$(cd "$(dirname "$0")" && pwd)"
trabajadores="${1:-1}"

if ! command -v docker > /dev/null 2>&1; then
    echo "falta docker; instala Docker Engine y el plugin docker compose" >&2
    exit 1
fi

exec docker compose \
    -f "$aqui/docker-compose.yml" \
    up --build \
    --abort-on-container-exit \
    --exit-code-from despachador \
    --scale "trabajador=$trabajadores"
