# El despachador necesita generar el lote si la entrada está vacía; el
# generador es C++ y se compila aquí para no depender de Python para escribir
# 64 PPM de 1024x1024.
FROM gcc:12-bookworm AS construccion
WORKDIR /src
COPY motor /src/motor
RUN make -C motor generar

FROM python:3.12-slim-bookworm

RUN apt-get update \
    && apt-get install -y --no-install-recommends libgomp1 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=construccion /src/motor/generar /usr/local/bin/generar
WORKDIR /app
COPY despachador/despachador.py /app/despachador.py

ENV ENTRADA=/datos/entrada \
    SALIDA=/datos/salida \
    CSV_SERVICIO=/datos/servicio.csv \
    TRABAJADOR_URL=http://trabajador:8000 \
    FILTROS=gris,gaussiano,sobel,otsu \
    CONCURRENCIA=8 \
    IMAGENES=64 \
    ANCHO=1024 \
    ALTO=1024 \
    ENTORNO=local \
    PYTHONUNBUFFERED=1 \
    PYTHONDONTWRITEBYTECODE=1

# El despachador es el único contenedor que monta los volúmenes. En Compose
# corre como root para poder escribir en los bind mounts del host; en
# Kubernetes el Job lo ejecuta con runAsUser/fsGroup para el PVC.
CMD ["python", "/app/despachador.py"]
