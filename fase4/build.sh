#!/usr/bin/env bash
# Build do engine PoC do SPECTER (Fase 4) contra o libardour EMBUTIDO.
#
# Nao precisa buildar o Ardour inteiro: usamos os headers do fonte 9.8
# (fase4/src/ardour) + os .so do runtime embutido em vendor/ardour/lib
# (rode ./tools/vendor-ardour.sh se o vendor ainda nao existir).
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$HERE/src/ardour"
BUILD="$HERE/build"
OUT="$HERE/engine/spook-audio-engine"
ARD_LIBDIR="$HERE/vendor/ardour/lib"

# config headers gerados (ver build/libardour-config.h)
INC=(-I"$BUILD")
# Cada lib do Ardour publica o proprio dir como export_includes (waf `use=`).
# Reproduzimos isso adicionando todos os dirs de libs/ ao caminho de include.
for d in "$SRC"/libs/*/; do
  INC+=(-I"$d")
done
INC+=(-I"$SRC/libs/ctrl-interface/control_protocol")

DEFS=(
  -DPROGRAM_NAME='"SpookAudio"'
  -DVERSIONSTRING='"SpookAudio 0.1 (Ardour 9.8 core)"'
  -DLOCALEDIR='"'"$HERE/vendor/ardour/share/locale"'"'
  -DDATA_DIR='"'"$HERE/vendor/ardour/share"'"'
  -DCONFIG_DIR='"/etc"'
  -DPACKAGE='"spook-audio"'
)

# -DWAF_BUILD e OBRIGATORIO: e o que faz os headers do Ardour incluirem os
# *-config.h gerados (session.h:34 `#ifdef WAF_BUILD #include "libardour-config.h"`).
# Sem ele USE_TLSF fica indefinido -> ARDOUR::Session::_mempool vira
# PBD::ReallocPool (56 B) em vez de PBD::TLSF (40 B) = 16 B de divergencia de
# ABI em Session e SIGSEGV em get_routes(). Ver wscript:1331.
CXXFLAGS=(-std=c++17 -O2 -g -fvisibility=default -fPIC -w -DNDEBUG -DWAF_BUILD)

PKG_CFLAGS=$(pkg-config --cflags glibmm-2.4 giomm-2.4 glib-2.0 sigc++-2.0 libxml-2.0)
PKG_LIBS=$(pkg-config --libs glibmm-2.4 giomm-2.4 glib-2.0 sigc++-2.0 libxml-2.0)

echo "== compilando =="
g++ "${CXXFLAGS[@]}" "${DEFS[@]}" "${INC[@]}" $PKG_CFLAGS \
    -c "$HERE/engine/spook-audio-engine.cc" -o "$BUILD/spook-audio-engine.o"

echo "== linkando =="
# rpath relativo ao binario (engine/): subir um nivel ate vendor/ardour/lib.
# -rpath-link so importa no LINK (o $ORIGIN nao e resolvivel em link-time).
g++ "$BUILD/spook-audio-engine.o" -o "$OUT" \
    -L"$ARD_LIBDIR" -Wl,-rpath-link,"$ARD_LIBDIR" \
    -Wl,--disable-new-dtags -Wl,-rpath,'$ORIGIN/../vendor/ardour/lib' \
    -lardour -lpbd -levoral -ltemporal -lmidipp -laudiographer \
    $PKG_LIBS -lstdc++

echo "== ok -> $OUT =="
ls -la "$OUT"
