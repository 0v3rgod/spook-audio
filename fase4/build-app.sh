#!/usr/bin/env bash
# Build do SPECTER (Fase 4) — binario unico.
#
# Junta num so executavel:
#   - o core do Ardour (runtime EMBUTIDO em vendor/ardour/lib)
#   - o mixer do hardware (ALSA snd_ctl)
#   - a nossa interface (gtkmm-3.0)
#
# O runtime do Ardour vive em vendor/ardour (rode ./tools/vendor-ardour.sh
# para (re)copiar do pacote `ardour`; ver vendor/ardour/VERSION.txt). Em
# runtime o pacote NAO e necessario; no BUILD tambem nao (os headers estao em
# src/ardour, ja embutidos no projeto).
#
# ATENCAO — POR QUE gtkmm-3.0 E NAO gtkmm-4.0:
#   O libardour foi construido contra glibmm-2.4 + sigc++-2.0. O gtkmm-4.0 usa
#   glibmm-2.68 + sigc++-3.0:
#     * os headers do Ardour NAO COMPILAM com sigc++3 (pbd/crossthread.h:97 usa
#       `sigc::slot<bool,Glib::IOCondition>`, sintaxe removida no sigc++3);
#     * libsigc-2.0/libsigc-3.0 exportam 79 simbolos mangled em comum e
#       libglibmm-2.4/libglibmm-2.68 mais 1483 -> o linker dinamico interpoe um
#       sobre o outro e o processo quebra.
#   Com gtkmm-3.0 (glibmm-2.4/sigc++-2.0) so ha UMA familia carregada.
#   Ver ui/main_window.h e /tmp/opencode/mixtest.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$HERE/src/ardour"
BUILD="$HERE/build"
OBJ="$BUILD/app"
OUT="$HERE/spook-audio"
ARD_LIBDIR="$HERE/vendor/ardour/lib"

if [ ! -d "$ARD_LIBDIR" ]; then
  printf '%s\n' "vendor/ardour/lib nao existe — rode ./tools/vendor-ardour.sh (precisa do pacote ardour instalado apenas para copiar)." >&2
  exit 1
fi

mkdir -p "$OBJ"

# config headers gerados (ver build/libardour-config.h)
INC=(-I"$BUILD")
# Cada lib do Ardour publica o proprio dir como export_includes (waf `use=`).
for d in "$SRC"/libs/*/; do
  INC+=(-I"$d")
done
INC+=(-I"$SRC/libs/ctrl-interface/control_protocol")

DEFS=(
  -DPROGRAM_NAME='"SpookAudio"'
  -DVERSIONSTRING='"SpookAudio 0.1 (Ardour 9.8 core)"'
  -DPACKAGE='"spook-audio"'
)

# -DWAF_BUILD e OBRIGATORIO (ver PoC-ENGINE.md): sem ele USE_TLSF fica
# indefinido e ARDOUR::Session desloca 16 bytes (SIGSEGV em get_routes()).
CXXFLAGS=(-std=c++17 -O2 -g -fvisibility=default -fPIC -w -DNDEBUG -DWAF_BUILD -pthread)

# gtkmm-3.0 => glibmm-2.4 + giomm-2.4 + sigc++-2.0 (a familia do libardour).
PKG="gtkmm-3.0 libxml-2.0 alsa"
PKG_CFLAGS=$(pkg-config --cflags $PKG)
PKG_LIBS=$(pkg-config --libs $PKG)

# Fontes do app. Ordem de -I: primeiro a raiz do projeto (para "core/…", "ui/…").
SOURCES=(
  "$HERE/spook/main.cc"
  "$HERE/spook/core/log.cc"
  "$HERE/spook/core/engine.cc"
  "$HERE/spook/core/daw.cc"
  "$HERE/spook/core/plugins.cc"
  "$HERE/spook/core/scarlett.cc"
  "$HERE/spook/ui/main_window.cc"
)

echo "== compilando =="
OBJECTS=()
for src in "${SOURCES[@]}"; do
  # nome do objeto preservando o caminho relativo (main.o, core/engine.o, …)
  rel="${src#$HERE/spook/}"
  obj="$OBJ/${rel%.cc}.o"
  mkdir -p "$(dirname "$obj")"
  echo "   $rel"
  g++ "${CXXFLAGS[@]}" "${DEFS[@]}" -I"$HERE/spook" "${INC[@]}" $PKG_CFLAGS -c "$src" -o "$obj"
  OBJECTS+=("$obj")
done

echo "== linkando =="
# --disable-new-dtags grava DT_RPATH (e nao DT_RUNPATH) com $ORIGIN no caminho:
# assim vendor/ardour/lib vale tambem para as dependencias INDIRETAS do
# libardour (libsuil.so.0, libc.so.6 do bundle etc. so existem la — com RUNPATH
# o binario cru morria em "libsuil.so.0: not found"). O caminho e relativo ao
# executavel: mover/renomear a pasta do projeto nao quebra o binario.
# -rpath-link e so para o LINK: $ORIGIN nao e resolvivel em link-time, entao o
# linker precisa do caminho absoluto do vendor para achar as NEEDED indiretas.
g++ "${OBJECTS[@]}" -o "$OUT" \
    -L"$ARD_LIBDIR" -Wl,-rpath-link,"$ARD_LIBDIR" \
    -Wl,--disable-new-dtags -Wl,-rpath,'$ORIGIN/vendor/ardour/lib' \
    -lardour -lardourcp -lpbd -levoral -ltemporal -lmidipp -laudiographer \
    $PKG_LIBS -lstdc++

echo "== ok -> $OUT =="
ls -la "$OUT"
