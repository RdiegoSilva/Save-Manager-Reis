#!/bin/sh
# Compila o SaveManagerReis.elf usando a imagem Docker oficial do PS2DEV.
# Requer apenas o Docker instalado.
set -e
docker run --rm -v "$(pwd)":/src -w /src ps2dev/ps2dev:latest sh -c "make clean && make"
echo
echo "Pronto: $(pwd)/SaveManagerReis.elf"
