import math
import sys


def leer(ruta):
    with open(ruta, "rb") as f:
        datos = f.read()
    campos = []
    i = 0
    while len(campos) < 4:
        while datos[i:i + 1].isspace():
            i += 1
        if datos[i:i + 1] == b"#":
            while datos[i:i + 1] != b"\n":
                i += 1
            continue
        inicio = i
        while not datos[i:i + 1].isspace():
            i += 1
        campos.append(datos[inicio:i].decode())
    i += 1
    magico, ancho, alto, maximo = campos[0], int(campos[1]), int(campos[2]), int(campos[3])
    if maximo != 255 or magico not in ("P5", "P6"):
        raise ValueError("formato no soportado")
    canales = 3 if magico == "P6" else 1
    return ancho, alto, canales, list(datos[i:i + ancho * alto * canales])


def escribir(ruta, ancho, alto, canales, pixeles):
    magico = "P6" if canales == 3 else "P5"
    with open(ruta, "wb") as f:
        f.write(f"{magico}\n{ancho} {alto}\n255\n".encode())
        f.write(bytes(pixeles))


def redondear(v):
    r = math.floor(v + 0.5)
    return max(0, min(255, r))


def recortar(v, limite):
    return max(0, min(limite - 1, v))


def gaussiano(ancho, alto, canales, p):
    k = [1, 4, 6, 4, 1]
    salida = []
    for y in range(alto):
        for x in range(ancho):
            for c in range(canales):
                suma = 0
                for j in range(-2, 3):
                    for i in range(-2, 3):
                        xx = recortar(x + i, ancho)
                        yy = recortar(y + j, alto)
                        suma += k[j + 2] * k[i + 2] * p[(yy * ancho + xx) * canales + c]
                salida.append(redondear(suma / 256))
    return salida


def sobel(ancho, alto, p):
    def en(x, y):
        return p[recortar(y, alto) * ancho + recortar(x, ancho)]

    salida = []
    for y in range(alto):
        for x in range(ancho):
            gx = (en(x + 1, y - 1) + 2 * en(x + 1, y) + en(x + 1, y + 1)
                  - en(x - 1, y - 1) - 2 * en(x - 1, y) - en(x - 1, y + 1))
            gy = (en(x - 1, y + 1) + 2 * en(x, y + 1) + en(x + 1, y + 1)
                  - en(x - 1, y - 1) - 2 * en(x, y - 1) - en(x + 1, y - 1))
            salida.append(min(255, redondear(math.sqrt(gx * gx + gy * gy))))
    return salida


def main():
    if len(sys.argv) != 4 or sys.argv[1] not in ("gaussiano", "sobel"):
        print("uso: python3 referencia.py gaussiano|sobel <entrada> <salida>", file=sys.stderr)
        return 2
    ancho, alto, canales, p = leer(sys.argv[2])
    if sys.argv[1] == "gaussiano":
        escribir(sys.argv[3], ancho, alto, canales, gaussiano(ancho, alto, canales, p))
    else:
        if canales != 1:
            print("sobel necesita una imagen de un canal", file=sys.stderr)
            return 1
        escribir(sys.argv[3], ancho, alto, 1, sobel(ancho, alto, p))
    return 0


if __name__ == "__main__":
    sys.exit(main())
