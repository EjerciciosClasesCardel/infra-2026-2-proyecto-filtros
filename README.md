# Procesamiento de imágenes por lotes

Motor de filtros en C++ con OpenMP, un trabajador que lo expone por HTTP y un
despachador que reparte el lote entre varios trabajadores.

## Integrantes

| Nombre completo | Código | Correo institucional |
|---|---|---|
|  |  |  |
|  |  |  |
|  |  |  |

El informe está en [`docs/`](docs/).

## Qué trae el repositorio

```
motor/            el motor en C++ con OpenMP
  src/            lectura y escritura de PPM y PGM, y los filtros
  tests/          pruebas de corrección y el comparador byte a byte
  bench/          arnés de medición y generador de imágenes
trabajador/       capa HTTP delgada sobre el motor (solo biblioteca estándar)
despachador/      reparto del lote y agregación de resultados
entrada/          lote de entrada del servicio (ignorado por git)
salida/           resultados del servicio (ignorados por git)
mediciones/       las tablas que alimentan el informe
deploy/local/     el servicio completo con Docker Compose
deploy/nube/      los manifiestos del clúster
docs/             el informe, en cuatro archivos más el índice
```

Todos los filtros están implementados en versión secuencial y paralela:
escala de grises, desenfoque de caja 3x3, desenfoque gaussiano separable,
Sobel con la magnitud del gradiente, ecualización del histograma y umbral de
Otsu. Además hay variantes que solo existen para medir: el gaussiano directo
de 5x5, el gaussiano por bloques (optimización de localidad) y el Otsu con un
histograma compartido protegido con `atomic`.

## Construir y correr el motor

```console
$ cd motor
$ make
g++ -std=c++17 -O2 -Wall -Wextra -fopenmp -Isrc -o motor src/main.cpp src/filtros.cpp src/imagen.cpp
g++ -std=c++17 -O2 -Wall -Wextra -fopenmp -Isrc -o comparar tests/comparar.cpp
g++ -std=c++17 -O2 -Wall -Wextra -fopenmp -Isrc -o generar bench/generar.cpp src/imagen.cpp
g++ -std=c++17 -O2 -Wall -Wextra -fopenmp -Isrc -o arnes bench/arnes.cpp src/filtros.cpp src/imagen.cpp
```

El motor recibe la cadena de filtros separados por comas, la imagen de entrada
y la de salida. Con `--secuencial` corre la versión de un hilo:

```console
$ ./motor gris tests/datos/entrada.ppm /tmp/salida.pgm
$ ./motor caja,caja tests/datos/entrada.ppm /tmp/suave.ppm --secuencial
```

Con `-` en la entrada o en la salida el motor usa la entrada o la salida
estándar. Es lo que permite que el trabajador procese una imagen sin escribir
archivos temporales:

```console
$ cat tests/datos/entrada.ppm | ./motor gris,gaussiano,sobel,otsu - - > /tmp/salida.pgm
$ ./motor --salud
motor ok hilos=8
```

`--salud` imprime el número de hilos de OpenMP y termina con código 0; el
trabajador lo usa para `/readyz` y para la cabecera `X-Hilos`.

La cadena del lote es `gris,gaussiano,sobel,otsu`: entra un PPM de color y
sale un PGM binario. Cada filtro necesita cierto número de canales y entrega
otro, así que el motor recorre la cadena llevando la cuenta y la rechaza antes
de aplicar nada cuando un filtro no va a recibir lo que necesita. `gris` pide
tres canales y devuelve uno; `sobel` y `otsu` piden uno; `caja` y `gaussiano`
trabajan con cualquiera de los dos.

## Las pruebas

```console
$ make pruebas
Salida esperada, calculada aparte:
  ok    gris sobre la imagen de prueba
  ok    caja sobre la imagen de prueba
  ok    gaussiano separable sobre la imagen de prueba
  ok    Sobel sobre la imagen de prueba
  ok    Otsu sobre la imagen de prueba
  ok    ecualización sobre la imagen de prueba
Lectura y escritura:
  ok    gris rechaza una imagen de un canal
  ok    caja sobre un canal escribe P5
Paralelo contra secuencial, imagen de 512x512 y 1 y 8 hilos:
  ok    gris idéntico con 1 hilo(s)
  ok    gris idéntico con 8 hilo(s)
  ok    caja idéntico con 1 hilo(s)
  ok    caja idéntico con 8 hilo(s)
  ok    gaussiano idéntico con 1 hilo(s)
  ok    gaussiano idéntico con 8 hilo(s)
  ok    sobel idéntico con 1 hilo(s)
  ok    sobel idéntico con 8 hilo(s)
  ok    otsu idéntico con 1 hilo(s)
  ok    otsu idéntico con 8 hilo(s)
  ok    ecualizar idéntico con 1 hilo(s)
  ok    ecualizar idéntico con 8 hilo(s)
Optimización de localidad y variantes de medición:
  ok    gaussiano por bloques idéntico al separable
  ok    gaussiano directo 5x5 idéntico al separable
  ok    Otsu con histograma compartido idéntico al privado
Cadena completa gris,gaussiano,sobel,otsu:
  ok    cadena idéntica con 1 hilo(s)
  ok    cadena idéntica con 8 hilo(s)
Filtro desconocido:
  ok    la cadena con un filtro inexistente se rechaza
Cadenas que no se pueden aplicar:
  ok    sobel se rechaza antes de aplicar nada
  ok    gris,gris se rechaza antes de aplicar nada
  ok    otsu se rechaza antes de aplicar nada
  ok    ecualizar se rechaza antes de aplicar nada

todas las pruebas pasaron
```

Las salidas esperadas de `tests/datos/` se calcularon con una implementación
aparte de las mismas definiciones, así que una prueba que pasa dice que las
dos coinciden. Las pruebas de gaussiano, Sobel, Otsu y ecualización usan esas
salidas, y además comparan la versión paralela contra la secuencial con 1 y 8
hilos, y la cadena completa. `comparar` es quien decide, byte a byte.

## Medir

La imagen de medición es de 4096x4096 y no se versiona: la escribe el
generador, que produce siempre el mismo archivo.

```console
$ ./generar /tmp/medicion.ppm 4096 4096
escrito /tmp/medicion.ppm: 4096x4096, 50331648 bytes de píxeles
```

El arnés cronometra solo la llamada al filtro, con la imagen ya en memoria, y
escribe una fila por repetición. El número de hilos sale de
`OMP_NUM_THREADS`; el resumen de la mediana descarta la primera corrida.

```console
$ OMP_NUM_THREADS=1 ./arnes gris /tmp/medicion.ppm 3 --encabezado
filtro,hilos,repeticion,milisegundos
gris,1,1,2.51226
gris,1,2,2.46808
gris,1,3,2.09325
gris con 1 hilo: mediana 2.28067 ms sobre 2 corridas
```

`bench/medir.sh` recorre 1, 2, 4 y 8 hilos para los filtros que se le pasen y
deja la tabla en `mediciones/motor.csv`:

```console
$ bash bench/medir.sh /tmp/medicion.ppm gris caja gaussiano
escrito ../mediciones/motor.csv con 60 filas
```

`bench/medir_completo.sh` hace la campaña entera: los filtros de color sobre
el PPM, los de un canal sobre su versión en grises y el gaussiano directo de
5x5 con un hilo. Deja las 185 filas de `mediciones/motor.csv`, que es la tabla
que alimenta el informe:

```console
$ ./motor gris /tmp/medicion.ppm /tmp/medicion.pgm
$ bash bench/medir_completo.sh /tmp/medicion.ppm /tmp/medicion.pgm
escrito ../mediciones/motor.csv con 185 filas
$ python3 bench/resumir.py ../mediciones/motor.csv
```

`bench/resumir.py` descarta la primera repetición de cada configuración y
escribe las tablas de T(p), S(p) y E(p) en Markdown.

## El servicio

El motor se expone por HTTP con dos piezas: el **trabajador** recibe una imagen,
ejecuta el binario `motor` y devuelve la imagen procesada; el **despachador**
lee el lote, reparte las peticiones entre varios trabajadores y escribe los
resultados. Los filtros siguen siendo C++: el trabajador solo transporta bytes
y no toca el disco, así que no necesita montar `entrada/` ni `salida/`.

| Método y ruta | Respuesta |
|---|---|
| `POST /procesar?filtros=gris,gaussiano,sobel,otsu` | Imagen procesada, con `X-Tiempo-Ms`, `X-Hilos` y `X-Filtros` |
| `GET /healthz` | `200 ok` si el proceso HTTP está vivo |
| `GET /readyz` | `200` si además `motor --salud` responde; `503` si no |

Un filtro desconocido o una imagen inválida devuelven HTTP 400. El detalle del
contrato, la concurrencia y el reparto están en
[`docs/01-arquitectura.md`](docs/01-arquitectura.md).

Para probarlo en local sin Docker, con el motor ya compilado:

```console
$ PUERTO=8000 RUTA_MOTOR=./motor/motor OMP_NUM_THREADS=1 \
    MAXIMO_EN_VUELO=2 python3 trabajador/app.py
$ ENTRADA=./entrada SALIDA=./salida CSV_SERVICIO=./mediciones/servicio.csv \
    TRABAJADOR_URL=http://127.0.0.1:8000 CONCURRENCIA=8 \
    RUTA_GENERAR=./motor/generar python3 despachador/despachador.py
```

Si `entrada/` no tiene archivos `.ppm`, el despachador genera 64 imágenes de
1024x1024 con `motor/generar`. Al terminar imprime el throughput y agrega una
fila a `mediciones/servicio.csv`.

### Docker Compose

Un solo comando construye las imágenes, levanta los trabajadores, ejecuta el
lote y baja todo al terminar. El argumento es el número de trabajadores:

```console
$ bash deploy/local/correr.sh 1
$ bash deploy/local/correr.sh 4
```

Las variables `OMP_NUM_THREADS`, `MAXIMO_EN_VUELO`, `CONCURRENCIA`, `FILTROS`,
`IMAGENES`, `ANCHO` y `ALTO` se pueden pasar por el entorno antes del script.

### Kubernetes / GKE

Los manifiestos de `deploy/nube/` crean el namespace, un ConfigMap, un PVC de
5 Gi, un Deployment de 3 réplicas, un Service ClusterIP y un Job para el
despachador. Las etiquetas de imagen se pasan al script y deben ser inmutables
(por ejemplo el SHA del commit), nunca `latest`:

```console
$ IMAGEN_TRABAJADOR="$REGISTRO/trabajador:$TAG" \
  IMAGEN_DESPACHADOR="$REGISTRO/despachador:$TAG" \
  bash deploy/nube/aplicar.sh
```

El script sustituye las imágenes, espera a las réplicas, ejecuta el Job y
muestra sus logs. El CSV completo queda en el PVC. Las instrucciones de
publicación de imágenes, los recursos y las sondas están en
[`docs/03-despliegue.md`](docs/03-despliegue.md).

### Throughput medido

Lote de 64 imágenes de 1024x1024 con la cadena `gris,gaussiano,sobel,otsu` y
`CONCURRENCIA=8`, medido con Docker Compose real (`deploy/local/correr.sh`):

| Trabajadores | Mediana (s) | Imágenes/s | Speedup |
|---:|---:|---:|---:|
| 1 | 1.220 | 52.5 | 1.00× |
| 4 | 0.443 | 144.5 | 2.75× |

Los mismos procesos sin contenedores dan 1.079 s y 0.399 s (2.70×), así que
Docker añade entre un 11 % y un 13 % de tiempo pero no cambia el escalado. Los
datos crudos están en `mediciones/servicio.csv`. El detalle de la medición y la
explicación del escalado están en
[`docs/03-despliegue.md`](docs/03-despliegue.md).

## Convenciones que no cambian

De ellas depende que la comparación byte a byte signifique algo.

1. **Bordes por repetición del píxel más cercano.** La vecindad que se sale de
   la imagen usa el píxel del borde: las coordenadas se recortan a
   `[0, ancho-1]` y `[0, alto-1]`. No se rellena con ceros ni se recorta la
   salida.
2. **Acumulación en `double` y redondeo al final**, al entero más cercano con
   los empates hacia arriba, saturando a `[0, 255]`. Están en `redondear()`.
3. **Las reducciones son sobre enteros.** El histograma acumula contadores
   enteros y Otsu trabaja sobre el histograma, así que el resultado no depende
   del orden en que los hilos acumulen.
