#!/usr/bin/env python3
"""Trabajador HTTP del servicio de filtros.

La capa Python solo lee la petición, ejecuta el binario C++ ``motor`` con la
imagen por la entrada estándar y devuelve la imagen resultante. No implementa
ningún filtro ni escribe archivos: el trabajador no toca el disco compartido.

Rutas
-----
``POST /procesar?filtros=...``
    Cuerpo binario con un PPM/PGM. La respuesta es la imagen procesada, con
    ``X-Tiempo-Ms`` y ``X-Hilos``. Un filtro desconocido o una imagen inválida
    producen HTTP 400.

``GET /healthz``
    El proceso HTTP está vivo.

``GET /readyz``
    Además, el binario ``motor`` responde a ``--salud``.

Variables de entorno
--------------------
``PUERTO``                puerto de escucha (8000)
``RUTA_MOTOR``            ruta del binario motor (se busca en PATH)
``FILTROS_POR_DEFECTO``   cadena usada cuando la petición no trae ``filtros``
``TAMANO_MAXIMO``         bytes máximos del cuerpo (64 MiB)
``MAXIMO_EN_VUELO``       peticiones que puede atender a la vez (2)
``TIEMPO_MAXIMO``         segundos máximos por imagen (60)
"""

import http.server
import os
import shutil
import signal
import subprocess
import sys
import threading
import time
import urllib.parse

PUERTO = int(os.environ.get("PUERTO", "8000"))
RUTA_MOTOR = os.environ.get("RUTA_MOTOR") or shutil.which("motor") or "motor"
FILTROS_POR_DEFECTO = os.environ.get("FILTROS_POR_DEFECTO", "gris,gaussiano,sobel,otsu")
TAMANO_MAXIMO = int(os.environ.get("TAMANO_MAXIMO", str(64 * 1024 * 1024)))
MAXIMO_EN_VUELO = max(1, int(os.environ.get("MAXIMO_EN_VUELO", "2")))
TIEMPO_MAXIMO = float(os.environ.get("TIEMPO_MAXIMO", "60"))
TIEMPO_SALUD = min(10.0, TIEMPO_MAXIMO)
# Esperar un turno no debería consumir todo el tiempo de la petición.
TIEMPO_ESPERA = min(30.0, TIEMPO_MAXIMO)

# Se llena al arrancar con la salida de ``motor --salud``. El trabajador lo
# reporta en X-Hilos para que el cliente sepa cuántos hilos usa OpenMP.
HILOS = 0
SEMAFORO = threading.BoundedSemaphore(MAXIMO_EN_VUELO)


def consultar_salud():
    """Ejecuta ``motor --salud`` y devuelve el número de hilos de OpenMP."""
    resultado = subprocess.run(
        [RUTA_MOTOR, "--salud"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=TIEMPO_SALUD,
        check=False,
    )
    if resultado.returncode != 0:
        detalle = resultado.stderr.decode("utf-8", "replace").strip()
        raise RuntimeError(detalle or f"motor terminó con código {resultado.returncode}")

    salida = resultado.stdout.decode("utf-8", "replace").strip()
    for pieza in salida.split():
        if pieza.startswith("hilos="):
            try:
                return int(pieza.split("=", 1)[1])
            except ValueError:
                break
    # Un motor que arranca pero no reporta hilos sigue siendo utilizable.
    return int(os.environ.get("OMP_NUM_THREADS", "1"))


class Servidor(http.server.ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True


class Manejador(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "trabajador-filtros/1.0"
    sys_version = ""

    def log_message(self, formato, *args):
        sys.stderr.write(
            "%s %s - %s\n" % (time.strftime("%H:%M:%S"), self.address_string(), formato % args)
        )
        sys.stderr.flush()

    def responder(self, codigo, cuerpo, tipo="text/plain; charset=utf-8", cabeceras=None):
        if isinstance(cuerpo, str):
            cuerpo = cuerpo.encode("utf-8")
        self.send_response(codigo)
        self.send_header("Content-Type", tipo)
        self.send_header("Content-Length", str(len(cuerpo)))
        if codigo >= 400:
            # Si no se consumió el cuerpo, reutilizar la conexión la dejaría
            # fuera de sincronía. Cerrar es más simple y no cuesta nada.
            self.send_header("Connection", "close")
            self.close_connection = True
        for nombre, valor in (cabeceras or {}).items():
            self.send_header(nombre, valor)
        self.end_headers()
        if self.command != "HEAD" and cuerpo:
            self.wfile.write(cuerpo)

    def do_GET(self):
        ruta = urllib.parse.urlsplit(self.path).path
        if ruta == "/healthz":
            self.responder(200, "ok\n")
            return
        if ruta == "/readyz":
            try:
                hilos = consultar_salud()
            except Exception as error:  # noqa: BLE001 - se reporta al cliente
                self.responder(503, f"no listo: {error}\n")
                return
            self.responder(
                200,
                f"listo hilos={hilos}\n",
                cabeceras={"X-Hilos": str(hilos)},
            )
            return
        self.responder(404, "ruta desconocida\n")

    def do_HEAD(self):
        self.do_GET()

    def do_POST(self):
        partes = urllib.parse.urlsplit(self.path)
        if partes.path != "/procesar":
            self.responder(404, "ruta desconocida\n")
            return

        parametros = urllib.parse.parse_qs(partes.query)
        filtros = (parametros.get("filtros") or [FILTROS_POR_DEFECTO])[0].strip()
        if not filtros:
            filtros = FILTROS_POR_DEFECTO

        largo_texto = self.headers.get("Content-Length")
        if largo_texto is None:
            self.responder(411, "falta Content-Length\n")
            return
        try:
            largo = int(largo_texto)
        except ValueError:
            self.responder(400, "Content-Length inválido\n")
            return
        if largo <= 0:
            self.responder(400, "cuerpo vacío\n")
            return
        if largo > TAMANO_MAXIMO:
            self.responder(413, f"imagen demasiado grande (máximo {TAMANO_MAXIMO} bytes)\n")
            return

        cuerpo = self.rfile.read(largo)
        if len(cuerpo) != largo:
            self.responder(400, "cuerpo incompleto\n")
            return

        if not SEMAFORO.acquire(timeout=TIEMPO_ESPERA):
            self.responder(503, "trabajador ocupado\n")
            return
        try:
            inicio = time.perf_counter()
            try:
                proceso = subprocess.run(
                    [RUTA_MOTOR, filtros, "-", "-"],
                    input=cuerpo,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    timeout=TIEMPO_MAXIMO,
                    check=False,
                )
            except subprocess.TimeoutExpired:
                self.responder(504, "el motor excedió el tiempo máximo\n")
                return
            except OSError as error:
                self.responder(500, f"no se pudo ejecutar el motor: {error}\n")
                return
            transcurrido_ms = (time.perf_counter() - inicio) * 1000.0
        finally:
            SEMAFORO.release()

        if proceso.returncode < 0:
            self.responder(500, "el motor terminó de forma anormal\n")
            return
        if proceso.returncode != 0:
            detalle = proceso.stderr.decode("utf-8", "replace").strip()
            self.responder(400, (detalle or "el motor rechazó la petición") + "\n")
            return
        if not proceso.stdout:
            self.responder(500, "el motor no devolvió ninguna imagen\n")
            return

        self.responder(
            200,
            proceso.stdout,
            tipo="application/octet-stream",
            cabeceras={
                "X-Tiempo-Ms": f"{transcurrido_ms:.3f}",
                "X-Hilos": str(HILOS),
                "X-Filtros": filtros,
            },
        )


def main():
    global HILOS
    try:
        HILOS = consultar_salud()
    except Exception as error:  # noqa: BLE001 - el proceso sigue vivo para /healthz
        HILOS = int(os.environ.get("OMP_NUM_THREADS", "1"))
        print(f"trabajador: el motor no está listo: {error}", file=sys.stderr, flush=True)

    servidor = Servidor(("0.0.0.0", PUERTO), Manejador)
    print(
        "trabajador escuchando en 0.0.0.0:%d (motor=%s, hilos=%s, en vuelo=%d)"
        % (PUERTO, RUTA_MOTOR, HILOS, MAXIMO_EN_VUELO),
        flush=True,
    )

    def apagar(_signum, _frame):
        # shutdown() tiene que llamarse desde otro hilo que serve_forever().
        threading.Thread(target=servidor.shutdown, daemon=True).start()

    signal.signal(signal.SIGTERM, apagar)
    signal.signal(signal.SIGINT, apagar)

    try:
        servidor.serve_forever(poll_interval=0.5)
    finally:
        servidor.server_close()


if __name__ == "__main__":
    main()
