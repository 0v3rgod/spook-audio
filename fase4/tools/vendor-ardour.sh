#!/usr/bin/env bash
# Copia (ou re-copia) o runtime do Ardour do sistema para vendor/ardour.
#
# O SPECTER nao depende do pacote `ardour` em tempo de execucao: tudo que o
# libardour le em runtime (DLLs + dados + config de sistema) mora em
# vendor/ardour. Rode este script DEPOIS de atualizar o pacote `ardour`
# (ou de instala-lo num clone novo, so para re-vendorizar), e entao re-build:
#   ./tools/vendor-ardour.sh && ./build-app.sh && ./app.sh --self-test
#
# Mapa:
#   vendor/ardour/lib   <- /usr/lib/ardour9  (bundle completo; inclui
#                          glibc-hwcaps, LV2, backends, engines, panners,
#                          surfaces, utils, vamp, fst, ladspa)
#   vendor/ardour/share <- /usr/share/ardour9  MENOS media/ e web_surfaces/
#                          (clip browser de MIDI e surfaces web do
#                          gtk2_ardour — nao usados pelo SPECTER; ~74 MB)
#   vendor/ardour/etc   <- /etc/ardour9 (system_config, keys/menus, clearlooks)
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
V="$HERE/vendor/ardour"
SRC_LIB=/usr/lib/ardour9
SRC_SHARE=/usr/share/ardour9
SRC_ETC=/etc/ardour9

for d in "$SRC_LIB" "$SRC_SHARE" "$SRC_ETC"; do
  if [ ! -d "$d" ]; then
    printf 'nao existe: %s — instale o pacote ardour para re-vendorizar\n' "$d" >&2
    exit 1
  fi
done

mkdir -p "$V/share" "$V/etc"

rm -rf "$V/lib"
cp -a "$SRC_LIB" "$V/lib"

( cd "$SRC_SHARE" && for x in *; do
    case "$x" in
      media|web_surfaces)
        printf 'pulando %s (nao usado pelo SPECTER)\n' "$x"
        continue
        ;;
    esac
    rm -rf "$V/share/$x"
    cp -a "$x" "$V/share/"
  done )

rm -rf "$V/etc"
mkdir -p "$V/etc"
cp -a "$SRC_ETC/." "$V/etc/"

{
  printf 'Vendored Ardour runtime for SPECTER\n'
  printf 'pacote: %s\n' "$(pacman -Q ardour 2>/dev/null || echo '(desconhecido)')"
  printf 'copiado: %s\n' "$(date -Iseconds)"
  printf 'lib/ = /usr/lib/ardour9 completo (bundle; inclui glibc-hwcaps, LV2, backends, engines, panners, surfaces, utils, vamp, fst, ladspa)\n'
  printf 'share/ = /usr/share/ardour9 exceto media/ e web_surfaces/ (clip browser e surfaces web do gtk2_ardour)\n'
  printf 'etc/ = /etc/ardour9 (system_config, keys/menus, clearlooks)\n'
  printf 'refresh: tools/vendor-ardour.sh\n'
} > "$V/VERSION.txt"

du -sh "$V"
printf 'ok: %s\n' "$V"
