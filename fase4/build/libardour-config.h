/* libardour-config.h — GERADO A MAO.
 *
 * Replica o que `libs/ardour/wscript:conf.write_config_header('libardour-config.h')`
 * escreve para o build do Arch (PKGBUILD oficial, --with-backends="alsa,dummy,jack,pulseaudio").
 *
 * POR QUE ISTO PRECISA BATER: alguns destes macros entram DENTRO de classes do
 * libardour (ex.: USE_TLSF troca PBD::ReallocPool por PBD::TLSF dentro de
 * ARDOUR::Session) -> se divergir, o ABI quebra silenciosamente.
 */
#pragma once

/* incondicional no wscript */
#define USE_TLSF 1

/* lv2 >= 1.2.0 -> mandatory; aqui ha lv2 1.18.10 */
#define LV2_SUPPORT 1
#define LV2_EXTENDED 1
#define HAVE_LV2_1_18_6 1

/* checks de header do wscript */
#define HAVE_SYS_VFS_H 1
#define HAVE_SYS_STATVFS_H 1
#define HAVE_IOPRIO 1

/* conf.define('CURRENT_SESSION_FILE_VERSION', ...) em libs/ardour/wscript */
#define CURRENT_SESSION_FILE_VERSION 7003

/* NOTA: BEATBOX nao e passado pelo PKGBUILD do Arch. Alem disso route.h checa
 * HAVE_BEATBOX (nome diferente) -> o bloco e codigo morto. Nao definir. */
