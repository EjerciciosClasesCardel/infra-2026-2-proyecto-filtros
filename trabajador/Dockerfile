# Etapa de compilación: el motor se compila con OpenMP y se copia al runtime.
FROM gcc:12-bookworm AS construccion
WORKDIR /src
COPY motor /src/motor
RUN make -C motor motor

FROM python:3.12-slim-bookworm

# libgomp1 es la biblioteca de OpenMP que necesita el motor en ejecución.
RUN apt-get update \
    && apt-get install -y --no-install-recommends libgomp1 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=construccion /src/motor/motor /usr/local/bin/motor
WORKDIR /app
COPY trabajador/app.py /app/app.py

ENV PUERTO=8000 \
    RUTA_MOTOR=/usr/local/bin/motor \
    OMP_NUM_THREADS=1 \
    MAXIMO_EN_VUELO=2 \
    PYTHONUNBUFFERED=1 \
    PYTHONDONTWRITEBYTECODE=1

EXPOSE 8000

# El trabajador no escribe archivos: no necesita un usuario con home ni un
# volumen. El UID arbitrario evita correr como root y coincide con el
# runAsUser de Kubernetes.
USER 10001:10001

HEALTHCHECK --interval=10s --timeout=3s --start-period=5s --retries=3 \
    CMD python -c "import os, urllib.request; urllib.request.urlopen('http://127.0.0.1:' + os.environ.get('PUERTO', '8000') + '/readyz', timeout=2)"

CMD ["python", "/app/app.py"]
