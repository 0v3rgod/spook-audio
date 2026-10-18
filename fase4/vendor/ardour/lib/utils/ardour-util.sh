#!/bin/sh

# This is a wrapper script for using the ardour-session utilities
# it is intended to be symlinked into $PATH for every session-tool

export GTK_PATH=/etc/ardour9:/usr/lib/ardour9${GTK_PATH:+:$GTK_PATH}
export LD_LIBRARY_PATH=/usr/lib/ardour9${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}

# Running Ardour requires these 3 variables to be set

export ARDOUR_DATA_PATH=/usr/share/ardour9
export ARDOUR_CONFIG_PATH=/etc/ardour9
export ARDOUR_DLL_PATH=/usr/lib/ardour9
export VAMP_PATH=/usr/lib/ardour9/vamp

SELF=`basename $0`
exec "/usr/lib/ardour9/utils/$SELF" "$@"
