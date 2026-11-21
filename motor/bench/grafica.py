import csv
import statistics
import sys
from collections import defaultdict

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt


def cargar_medianas(ruta):
    corridas = defaultdict(list)

    with open(ruta, newline="") as archivo:
        for fila in csv.DictReader(archivo):
            filtro = fila["filtro"]
            if filtro == "filtro":
                continue

            hilos = int(fila["hilos"])
            repeticion = int(fila["repeticion"])
            tiempo = float(fila["milisegundos"])

            corridas[(filtro, hilos)].append((repeticion, tiempo))

    medianas = {}
    for clave, valores in corridas.items():
        utiles = [
            tiempo for rep, tiempo in sorted(valores) if rep > 1
        ] or [tiempo for _, tiempo in valores]

        medianas[clave] = statistics.median(utiles)

    return medianas


def main():
    if len(sys.argv) != 2:
        print("Uso: python3 bench/grafica.py <mediciones.csv>")
        return 2

    medianas = cargar_medianas(sys.argv[1])
    filtros = [
        filtro for filtro in dict.fromkeys(f for f, _ in medianas)
        if not filtro.endswith("_secuencial")
    ]

    hilos_disponibles = sorted({
        hilos for filtro, hilos in medianas
        if filtro in filtros
    })

    # Gráfica 1: tiempos de ejecución
    fig, ax = plt.subplots(figsize=(12, 7))

    for filtro in filtros:
        puntos = [
            (h, medianas[(filtro, h)])
            for h in hilos_disponibles
            if (filtro, h) in medianas
        ]
        if puntos:
            x, y = zip(*puntos)
            ax.plot(x, y, marker="o", label=filtro)

    ax.set_title("Tiempo de ejecución según número de hilos")
    ax.set_xlabel("Número de hilos")
    ax.set_ylabel("Tiempo (ms)")
    ax.set_xticks(hilos_disponibles)
    ax.grid(True, alpha=0.3)
    ax.legend(fontsize=8, bbox_to_anchor=(1.02, 1), loc="upper left")
    fig.tight_layout()
    fig.savefig("../mediciones/tiempos_motor.png", dpi=180,
                bbox_inches="tight")
    plt.close(fig)

    # Gráfica 2: speedup respecto a la versión paralela con un hilo
    fig, ax = plt.subplots(figsize=(12, 7))

    for filtro in filtros:
        base = medianas.get((filtro, 1))
        if not base:
            continue

        puntos = [
            (h, base / medianas[(filtro, h)])
            for h in hilos_disponibles
            if (filtro, h) in medianas
        ]
        if puntos:
            x, y = zip(*puntos)
            ax.plot(x, y, marker="o", label=filtro)

    ax.plot(hilos_disponibles, hilos_disponibles,
            linestyle="--", label="Aceleración ideal")
    ax.set_title("Speedup respecto a la ejecución con un hilo")
    ax.set_xlabel("Número de hilos")
    ax.set_ylabel("Speedup")
    ax.set_xticks(hilos_disponibles)
    ax.grid(True, alpha=0.3)
    ax.legend(fontsize=8, bbox_to_anchor=(1.02, 1), loc="upper left")
    fig.tight_layout()
    fig.savefig("../mediciones/speedup_motor.png", dpi=180,
                bbox_inches="tight")
    plt.close(fig)

    print("Gráficas generadas:")
    print("../mediciones/tiempos_motor.png")
    print("../mediciones/speedup_motor.png")
    return 0


if __name__ == "__main__":
    sys.exit(main())
