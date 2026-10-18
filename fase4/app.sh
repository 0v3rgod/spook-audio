#!/usr/bin/env bash
# Launcher do SPECTER (binario unico).
#
# O runtime do Ardour vai EMBUTIDO em vendor/ardour:
#   lib   <- /usr/lib/ardour9   (bundle completo, inclui glibc-hwcaps/LV2/...)
#   share <- /usr/share/ardour9 (sem media/ e web_surfaces/)
#   etc   <- /etc/ardour9
# O app NAO depende do pacote `ardour` instalado; se o vendor nao existir
# (clone novo antes de ./tools/vendor-ardour.sh), cai para os caminhos do
# sistema. Os mesmos defaults sao aplicados dentro do binario (engine.cc),
# isto aqui e so o caminho explicito.
#
# Modos:
#   ./app.sh                  abre a janela
#   ./app.sh --self-test      teste de fumaca sem GUI (backend Dummy, sem som)
#   ./app.sh --backends       lista os backends e sai
#
# Teste sem a sessao grafica do usuario (regra do AGENTS.md: nunca abrir janela
# na sessao Wayland do Taylor):
#   Xvfb :99 &  DISPLAY=:99 SPOOK_NON_UNIQUE=1 ./app.sh
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [ -d "$HERE/vendor/ardour/lib" ]; then
  ARD_LIB="$HERE/vendor/ardour/lib"
  ARD_SHARE="$HERE/vendor/ardour/share"
  ARD_ETC="$HERE/vendor/ardour/etc"
else
  ARD_LIB=/usr/lib/ardour9
  ARD_SHARE=/usr/share/ardour9
  ARD_ETC=/etc/ardour9
fi

export LD_LIBRARY_PATH="$ARD_LIB${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export ARDOUR_DATA_PATH="$ARD_SHARE"
export ARDOUR_CONFIG_PATH="$ARD_ETC"
export ARDOUR_DLL_PATH="$ARD_LIB"
export VAMP_PATH="$ARD_LIB/vamp"
export GTK2_RC_FILES=/dev/null

exec "$HERE/spook-audio" "$@"
