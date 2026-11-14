#!/usr/bin/env python3
"""Despachador del lote de imágenes.

Es el único proceso que toca el almacenamiento compartido: lee las imágenes de
``ENTRADA``, las envía por HTTP a los trabajadores y escribe las respuestas en
``SALIDA``. Los trabajadores solo reciben y devuelven bytes.

El reparto usa un grupo de hilos (``concurrent.futures``) y una lista de URLs
de trabajadores que rota con un candado. Si ``trabajador`` resuelve a varias
direcciones, como ocurre con un servicio de Compose escalado, cada petición
va a una distinta. En Kubernetes el nombre resuelve al ClusterIP del Service y
es kube-proxy quien reparte las conexiones.

Variables de entorno
--------------------
``ENTRADA``, ``SALIDA``       directorios del lote (/datos/entrada, /datos/salida)
``CSV_SERVICIO``              archivo de resultados (/datos/servicio.csv)
``ENTORNO``                   etiqueta de la corrida (local, gke, ...)
``TRABAJADOR_URL``            URL del servicio (http://trabajador:8000)
``TRABAJADOR_URLS``           lista explícita separada por comas, para pruebas
``FILTROS``                   cadena de filtros (gris,gaussiano,sobel,otsu)
``CONCURRENCIA``              peticiones en vuelo (8)
``IMAGENES``, ``ANCHO``, ``ALTO``  lote a generar si ENTRADA está vacío
``TRABAJADORES``              número de réplicas para el CSV; 0 = inferirlo
``REINTENTOS``, ``ESPERA_REINTENTO``  tolerancia a trabajadores que aún no están
"""

import concurrent.futures
import csv
import http.client
import math
import os
import shutil
import socket
import subprocess
import sys
import threading
import time
import urllib.parse
from pathlib import Path

ENTRADA = Path(os.environ.get("ENTRADA", "/datos/entrada"))
SALIDA = Path(os.environ.get("SALIDA", "/datos/salida"))
CSV_SERVICIO = Path(os.environ.get("CSV_SERVICIO", "/datos/servicio.csv"))
ENTORNO = os.environ.get("ENTORNO", "local")
TRABAJADOR_URL = os.environ.get("TRABAJADOR_URL", "http://trabajador:8000")
TRABAJADOR_URLS = os.environ.get("TRABAJADOR_URLS", "").strip()
FILTROS = os.environ.get("FILTROS", "gris,gaussiano,sobel,otsu")
CONCURRENCIA = max(1, int(os.environ.get("CONCURRENCIA", "8")))
IMAGENES = max(1, int(os.environ.get("IMAGENES", "64")))
ANCHO = int(os.environ.get("ANCHO", "1024"))
ALTO = int(os.environ.get("ALTO", "1024"))
TRABAJADORES = int(os.environ.get("TRABAJADORES", "0"))
REINTENTOS = max(1, int(os.environ.get("REINTENTOS", "20")))
ESPERA_REINTENTO = float(os.environ.get("ESPERA_REINTENTO", "0.5"))
TIEMPO_MAXIMO = float(os.environ.get("TIEMPO_MAXIMO", "120"))
RUTA_GENERAR = os.environ.get("RUTA_GENERAR") or shutil.which("generar") or "generar"


class Reparto:
    """Entrega una URL distinta en cada llamada, en orden circular."""

    def __init__(self, urls):
        if not urls:
            raise ValueError("no hay trabajadores a los que repartir")
        self.urls = list(urls)
        self._indice = 0
        self._candado = threading.Lock()

    def siguiente(self):
        with self._candado:
            url = self.urls[self._indice % len(self.urls)]
            self._indice += 1
            return url


class Cliente:
    """Cliente HTTP mínimo; una conexión por petición y sin estados globales."""

    def __init__(self, tiempo_maximo):
        self.tiempo_maximo = tiempo_maximo

    def post(self, url, ruta, cuerpo, cabeceras):
        partes = urllib.parse.urlsplit(url)
        clase = http.client.HTTPSConnection if partes.scheme == "https" else http.client.HTTPConnection
        puerto = partes.port or (443 if partes.scheme == "https" else 80)
        conexion = clase(partes.hostname, puerto, timeout=self.tiempo_maximo)
        try:
            conexion.request("POST", ruta, body=cuerpo, headers=cabeceras)
            respuesta = conexion.getresponse()
            datos = respuesta.read()
            cabeceras_respuesta = {nombre.lower(): valor for nombre, valor in respuesta.getheaders()}
            return respuesta.status, datos, cabeceras_respuesta
        finally:
            conexion.close()


def construir_urls():
    """Expande el nombre del servicio a una URL por dirección resuelta."""
    if TRABAJADOR_URLS:
        urls = [url.strip().rstrip("/") for url in TRABAJADOR_URLS.split(",") if url.strip()]
        if urls:
            return urls

    partes = urllib.parse.urlsplit(TRABAJADOR_URL)
    if not partes.hostname:
        raise RuntimeError(f"TRABAJADOR_URL no tiene nombre de host: {TRABAJADOR_URL}")
    esquema = partes.scheme or "http"
    puerto = partes.port or (443 if esquema == "https" else 80)
    ruta = partes.path.rstrip("/")

    direcciones = []
    for _familia, _tipo, _proto, _canon, direccion in socket.getaddrinfo(
        partes.hostname, puerto, type=socket.SOCK_STREAM
    ):
        ip = direccion[0]
        # Una IPv6 con scope no se puede escribir tal cual en una URL.
        ip = ip.split("%", 1)[0]
        if ip not in direcciones:
            direcciones.append(ip)

    urls = []
    for ip in direcciones:
        if ":" in ip:
            ip = f"[{ip}]"
        urls.append(f"{esquema}://{ip}:{puerto}{ruta}")
    return urls


def esperar_trabajadores():
    ultimo_error = None
    for intento in range(REINTENTOS):
        try:
            urls = construir_urls()
            if urls:
                return urls
            ultimo_error = RuntimeError("la resolución no devolvió direcciones")
        except (OSError, RuntimeError) as error:
            ultimo_error = error
        if intento + 1 < REINTENTOS:
            time.sleep(min(2.0, ESPERA_REINTENTO * (2 ** intento)))
    raise RuntimeError(f"no se pudo resolver el servicio de trabajadores: {ultimo_error}")


def listar_imagenes():
    if not ENTRADA.exists():
        return []
    return sorted(
        ruta for ruta in ENTRADA.iterdir() if ruta.is_file() and ruta.suffix.lower() == ".ppm"
    )


def generar_lote():
    ENTRADA.mkdir(parents=True, exist_ok=True)
    print(
        f"entrada vacía: generando {IMAGENES} imágenes de {ANCHO}x{ALTO} en {ENTRADA}",
        flush=True,
    )
    for indice in range(1, IMAGENES + 1):
        destino = ENTRADA / f"imagen_{indice:04d}.ppm"
        resultado = subprocess.run(
            [RUTA_GENERAR, str(destino), str(ANCHO), str(ALTO), str(indice)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        if resultado.returncode != 0:
            detalle = resultado.stderr.decode("utf-8", "replace").strip()
            raise RuntimeError(f"generar falló para {destino.name}: {detalle}")


def extension_de(datos):
    if datos.startswith(b"P6"):
        return ".ppm"
    if datos.startswith(b"P5"):
        return ".pgm"
    raise RuntimeError("la respuesta no tiene cabecera P5 ni P6")


def numero(cabeceras, nombre):
    try:
        return float(cabeceras.get(nombre, "nan"))
    except (TypeError, ValueError):
        return float("nan")


def procesar_una(cliente, reparto, ruta, filtros, salida):
    cuerpo = ruta.read_bytes()
    ruta_http = "/procesar?" + urllib.parse.urlencode({"filtros": filtros})
    cabeceras = {
        "Content-Type": "application/octet-stream",
        "Content-Length": str(len(cuerpo)),
        # Cerrar en cada petición deja que kube-proxy reparta también en
        # Kubernetes, donde el Service resuelve a un único ClusterIP.
        "Connection": "close",
    }

    ultimo_error = None
    for intento in range(REINTENTOS):
        url = reparto.siguiente()
        try:
            estado, datos, cabeceras_respuesta = cliente.post(url, ruta_http, cuerpo, cabeceras)
        except (OSError, http.client.HTTPException) as error:
            ultimo_error = error
        else:
            if estado == 200:
                break
            detalle = datos[:300].decode("utf-8", "replace").strip()
            if estado < 500:
                # Un 4xx no se arregla repitiendo: filtro o imagen inválidos.
                raise RuntimeError(f"{ruta.name}: HTTP {estado}: {detalle}")
            ultimo_error = RuntimeError(f"HTTP {estado}: {detalle}")
        if intento + 1 < REINTENTOS:
            time.sleep(min(2.0, ESPERA_REINTENTO * (2 ** intento)))
    else:
        raise RuntimeError(f"{ruta.name}: falló tras {REINTENTOS} intentos: {ultimo_error}")

    destino = salida / (ruta.stem + extension_de(datos))
    destino.write_bytes(datos)
    return {
        "entrada": ruta.name,
        "salida": destino.name,
        "bytes": len(datos),
        "motor_ms": numero(cabeceras_respuesta, "x-tiempo-ms"),
        "hilos": cabeceras_respuesta.get("x-hilos", "?"),
        "trabajador": url,
    }


def registrar_csv(trabajadores, imagenes, segundos):
    CSV_SERVICIO.parent.mkdir(parents=True, exist_ok=True)
    nuevo = not CSV_SERVICIO.exists() or CSV_SERVICIO.stat().st_size == 0
    with CSV_SERVICIO.open("a", newline="", encoding="utf-8") as archivo:
        escritor = csv.writer(archivo)
        if nuevo:
            escritor.writerow(["entorno", "trabajadores", "concurrencia", "imagenes", "segundos"])
        escritor.writerow([ENTORNO, trabajadores, CONCURRENCIA, imagenes, f"{segundos:.3f}"])


def main():
    SALIDA.mkdir(parents=True, exist_ok=True)
    ENTRADA.mkdir(parents=True, exist_ok=True)

    imagenes = listar_imagenes()
    if not imagenes:
        generar_lote()
        imagenes = listar_imagenes()
    if not imagenes:
        raise RuntimeError(f"no hay imágenes .ppm en {ENTRADA}")

    urls = esperar_trabajadores()
    trabajadores = TRABAJADORES if TRABAJADORES > 0 else len(urls)
    print(
        f"despachador: {len(imagenes)} imágenes, {trabajadores} trabajador(es), "
        f"{len(urls)} URL(s), concurrencia={CONCURRENCIA}, filtros={FILTROS}",
        flush=True,
    )
    for url in urls:
        print(f"  trabajador: {url}", flush=True)

    cliente = Cliente(TIEMPO_MAXIMO)
    reparto = Reparto(urls)
    resultados = []
    errores = []
    inicio = time.perf_counter()
    with concurrent.futures.ThreadPoolExecutor(max_workers=CONCURRENCIA) as ejecutor:
        futuros = {
            ejecutor.submit(procesar_una, cliente, reparto, ruta, FILTROS, SALIDA): ruta
            for ruta in imagenes
        }
        paso = max(1, len(imagenes) // 10)
        for futuro in concurrent.futures.as_completed(futuros):
            ruta = futuros[futuro]
            try:
                resultados.append(futuro.result())
            except Exception as error:  # noqa: BLE001 - se resume al final
                errores.append((ruta.name, error))
            terminadas = len(resultados) + len(errores)
            if terminadas % paso == 0:
                print(f"  {terminadas}/{len(imagenes)} procesadas", flush=True)
    segundos = time.perf_counter() - inicio

    if errores:
        print(f"\n{len(errores)} imagen(es) fallaron:", file=sys.stderr)
        for _nombre, error in errores:
            print(f"  {error}", file=sys.stderr)
        print(
            f"procesadas {len(resultados)}/{len(imagenes)} imágenes en {segundos:.3f} s",
            file=sys.stderr,
        )
        return 1

    media_motor = None
    tiempos = [
        resultado["motor_ms"]
        for resultado in resultados
        if not math.isnan(resultado["motor_ms"])
    ]
    if tiempos:
        media_motor = sum(tiempos) / len(tiempos)
    throughput = len(resultados) / segundos if segundos > 0 else float("inf")

    print(f"\nprocesadas {len(resultados)} imágenes en {segundos:.3f} s")
    print(f"throughput: {throughput:.2f} imágenes/s ({throughput * 60.0:.1f} imágenes/min)")
    if media_motor is not None:
        print(f"tiempo medio dentro del motor: {media_motor:.1f} ms")
    registrar_csv(trabajadores, len(resultados), segundos)
    print(f"fila CSV: {ENTORNO},{trabajadores},{CONCURRENCIA},{len(resultados)},{segundos:.3f}")
    print(f"resultado agregado en {CSV_SERVICIO}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:  # noqa: BLE001 - el Job debe terminar con error visible
        print(f"despachador: {error}", file=sys.stderr)
        sys.exit(1)
