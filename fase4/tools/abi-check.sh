#!/usr/bin/env bash
# Confere se o layout de ARDOUR::Session que NOSSOS headers produzem e igual ao do
# libardour INSTALADO. Depois de atualizar o pacote `ardour`, rode isto ANTES de
# culpar qualquer outra coisa: uma flag de compilacao faltando (ex.: -DWAF_BUILD)
# desloca membros em silencio e o sintoma e um SIGSEGV em qualquer funcao inline.
#
# Contexto completo em ../PoC-ENGINE.md.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FASE4="$(cd "$HERE/.." && pwd)"
ARD_LIB=/usr/lib/ardour9/libardour.so.3.0.0
WORK="${SPOOK_ABI_WORK:-/tmp/opencode/spook-abi}"
mkdir -p "$WORK"

echo "== 1/4  layout da lib instalada (DWARF via debuginfod) =="
if [ ! -s "$WORK/ptype.txt" ]; then
  DEBUGINFOD_URLS="${DEBUGINFOD_URLS:-https://debuginfod.archlinux.org}" \
    gdb -batch -q -iex "set debuginfod enabled on" \
        -ex "ptype /o ARDOUR::Session" "$ARD_LIB" > "$WORK/ptype.txt" 2>&1
fi
grep -q "total size" "$WORK/ptype.txt" || {
  echo "ERRO: o dump do ptype nao contem layout. Veja $WORK/ptype.txt"; exit 1; }

echo "== 2/4  tabela de membros =="
( cd "$WORK" && python3 "$HERE/abi.py" parse-lib ptype.txt )

echo "== 3/4  compilando a sonda offsetof contra os NOSSOS headers =="
# copia do session.h com private:/protected: -> public: para podermos usar offsetof().
# (NAO use -Dprivate=public global: quebra a libstdc++, ex. sstream.)
sed -e 's/^\(\s*\)private\s*:/\1public:/' \
    -e 's/^\(\s*\)protected\s*:/\1public:/' \
    "$FASE4/src/ardour/libs/ardour/ardour/session.h" > "$WORK/session_pub.h"
( cd "$WORK" && python3 "$HERE/abi.py" gen-probe lib_members.json session_pub.h offdump.cc )

SRC="$FASE4/src/ardour"
INC=(-I"$FASE4/build")
for d in "$SRC"/libs/*/; do INC+=(-I"$d"); done
INC+=(-I"$SRC/libs/ctrl-interface/control_protocol")
PKG=$(pkg-config --cflags glibmm-2.4 giomm-2.4 glib-2.0 sigc++-2.0 libxml-2.0)

# -DWAF_BUILD e -DNDEBUG TEM de ser iguais aos do build real (ver build.sh)
g++ -std=c++17 -O0 -DNDEBUG -DWAF_BUILD -w "${INC[@]}" $PKG \
    "$WORK/offdump.cc" -o "$WORK/offdump"

echo "== 4/4  comparando =="
"$WORK/offdump" > "$WORK/our_offsets.txt"
( cd "$WORK" && python3 "$HERE/abi.py" compare lib_members.json our_offsets.txt )
