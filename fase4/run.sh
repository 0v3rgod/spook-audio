#!/usr/bin/env bash
# Launcher do engine PoC do SPECTER.
#
# Usa o runtime EMBUTIDO em vendor/ardour (o app nao depende do pacote `ardour`);
# se o vendor nao existir, cai para os caminhos do sistema.
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

exec "$HERE/engine/spook-audio-engine" "$@"
