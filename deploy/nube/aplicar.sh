#!/usr/bin/env bash
# Aplica los manifiestos de deploy/nube/ con las etiquetas de imagen que se
# pasen. Las etiquetas tienen que ser inmutables (por ejemplo el SHA del
# commit), nunca latest.
#
#   IMAGEN_TRABAJADOR=.../trabajador:sha-abc123 \
#   IMAGEN_DESPACHADOR=.../despachador:sha-abc123 \
#   bash deploy/nube/aplicar.sh
set -euo pipefail

: "${IMAGEN_TRABAJADOR:?define IMAGEN_TRABAJADOR con una etiqueta inmutable}"
: "${IMAGEN_DESPACHADOR:?define IMAGEN_DESPACHADOR con una etiqueta inmutable}"

if ! command -v kubectl > /dev/null 2>&1; then
    echo "falta kubectl" >&2
    exit 1
fi

aqui="$(cd "$(dirname "$0")" && pwd)"

# sed usa | como separador porque las imágenes llevan / y :. Se escapan los
# caracteres que sed interpretaría dentro del reemplazo.
escapar() {
    printf '%s' "$1" | sed 's/[&|]/\\&/g'
}
trabajador_escapado="$(escapar "$IMAGEN_TRABAJADOR")"
despachador_escapado="$(escapar "$IMAGEN_DESPACHADOR")"

sustituir() {
    sed \
        -e "s|\${IMAGEN_TRABAJADOR}|$trabajador_escapado|g" \
        -e "s|\${IMAGEN_DESPACHADOR}|$despachador_escapado|g" \
        "$1"
}

for archivo in 00-namespace.yaml 01-configmap.yaml 02-pvc.yaml; do
    echo "aplicando $archivo"
    kubectl apply -f "$aqui/$archivo"
done

for archivo in 03-trabajador-deployment.yaml 04-trabajador-service.yaml; do
    echo "aplicando $archivo"
    sustituir "$aqui/$archivo" | kubectl apply -f -
done

echo "esperando a que las 3 réplicas estén listas"
kubectl -n filtros rollout status deployment/trabajador --timeout=300s

# Un Job terminado es inmutable; borrarlo permite volver a correr el lote.
kubectl -n filtros delete job despachador --ignore-not-found
echo "aplicando 05-despachador-job.yaml"
sustituir "$aqui/05-despachador-job.yaml" | kubectl apply -f -

if kubectl -n filtros wait --for=condition=complete job/despachador --timeout=10m; then
    echo "--- salida del despachador ---"
    kubectl -n filtros logs job/despachador
    echo
    echo "El CSV completo queda en /datos/servicio.csv del PVC filtros-datos."
else
    echo "el Job no terminó bien; sus logs:" >&2
    kubectl -n filtros logs job/despachador >&2 || true
    exit 1
fi
