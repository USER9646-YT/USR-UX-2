#!/bin/sh
# Lists the system headers that the Nano-X files we build ask for and that the libc
# (libc/include) and the port's own include directory don't provide.
# Run from the root of the OS source tree:  sh tools/nxheaders.sh

S=../ports/nanox/src

{
    cat $S/engine/*.c $S/nanox/*.c $S/include/*.h $S/demos/nanox/demo-hello.c
    for f in fb fblin1 fblin2 fblin4 fblin8 fblin16 fblin24 fblin32 genfont genmem \
             osdep rasterops scr_fb mou_devmice kbd_tty \
             fbportrait_left fbportrait_right fbportrait_down; do
        cat $S/drivers/$f.c
    done
} | sed -n 's/^[[:space:]]*#[[:space:]]*include[[:space:]]*<\([^>]*\)>.*/\1/p' | sort -u |
while read h; do
    [ -e ../libc/include/$h ] || [ -e $S/include/$h ] || echo "missing: $h"
done
