CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -fopenmp
BIN      := motor comparar generar arnes test_estencil

all: $(BIN)

motor: src/main.cpp src/filtros.cpp src/imagen.cpp src/gaussiano.cpp src/sobel.cpp
	$(CXX) $(CXXFLAGS) -Isrc -o $@ $^

comparar: tests/comparar.cpp
	$(CXX) $(CXXFLAGS) -Isrc -o $@ $^

generar: bench/generar.cpp src/imagen.cpp
	$(CXX) $(CXXFLAGS) -Isrc -o $@ $^

# Pruebas internas de gaussiano y Sobel (valores a mano, bordes, separable
# contra directo 5x5, paralelo contra secuencial con 1, 2, 4 y 8 hilos).
test_estencil: tests/test_estencil.cpp src/gaussiano.cpp src/sobel.cpp
	$(CXX) $(CXXFLAGS) -Isrc -o $@ $^

arnes: bench/arnes.cpp src/filtros.cpp src/imagen.cpp src/gaussiano.cpp src/sobel.cpp
	$(CXX) $(CXXFLAGS) -Isrc -o $@ $^

pruebas: all
	bash tests/pruebas.sh

clean:
	rm -f $(BIN)

.PHONY: all pruebas clean