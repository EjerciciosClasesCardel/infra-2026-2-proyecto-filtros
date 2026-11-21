
import csv
import statistics
import sys
from collections import defaultdict


def main():
    if len(sys.argv) != 2:
        print("uso: python3 bench/tabla.py <mediciones.csv>", file=sys.stderr)
        return 2

    corridas = defaultdict(list)

    with open(sys.argv[1], newline="") as f:
        for fila in csv.DictReader(f):
            clave = (fila["filtro"], int(fila["hilos"]))
            corridas[clave].append(
                (int(fila["repeticion"]), float(fila["milisegundos"]))
            )

    medianas = {}

    for clave, valores in corridas.items():
        utiles = [
            ms for rep, ms in sorted(valores) if rep > 1
        ] or [ms for _, ms in valores]

        medianas[clave] = statistics.median(utiles)

    filtros = sorted(
        {f for f, _ in medianas},
        key=lambda f: min(
            i for i, (g, _) in enumerate(corridas) if g == f
        )
    )

    for filtro in filtros:
        hilos = sorted(h for f, h in medianas if f == filtro)
        base = medianas.get((filtro, 1))

        print(f"\n**{filtro}**\n")
        print("| $p$ | $T(p)$ [ms] | $S(p)$ | $E(p)$ |")
        print("|---:|---:|---:|---:|")

        for p in hilos:
            t = medianas[(filtro, p)]
            s = base / t if base else float("nan")
            print(f"| {p} | {t:.2f} | {s:.2f} | {s / p:.2f} |")

    return 0


if __name__ == "__main__":
    sys.exit(main())

