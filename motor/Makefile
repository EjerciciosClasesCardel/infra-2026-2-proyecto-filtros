CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -fopenmp
BIN      := motor comparar generar arnes probar_histograma

all: $(BIN)

motor: src/main.cpp src/filtros.cpp src/imagen.cpp
	$(CXX) $(CXXFLAGS) -Isrc -o $@ $^

comparar: tests/comparar.cpp
	$(CXX) $(CXXFLAGS) -Isrc -o $@ $^

generar: bench/generar.cpp src/imagen.cpp
	$(CXX) $(CXXFLAGS) -Isrc -o $@ $^

arnes: bench/arnes.cpp src/filtros.cpp src/imagen.cpp
	$(CXX) $(CXXFLAGS) -Isrc -o $@ $^

probar_histograma: tests/histograma.cpp src/filtros.cpp src/imagen.cpp
	$(CXX) $(CXXFLAGS) -Isrc -o $@ $^

pruebas: all
	bash tests/pruebas.sh

clean:
	rm -f $(BIN)

.PHONY: all pruebas clean
