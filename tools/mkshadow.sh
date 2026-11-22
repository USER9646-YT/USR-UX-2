#!/bin/sh
# usage: sh tools/mkshadow.sh user:password ...   -> prints /etc/shadow lines
for entry in "$@"; do
    user=${entry%%:*}
    pass=${entry#*:}
    salt=$(head -c 6 /dev/urandom | od -An -tx1 | tr -d ' \n')
    hash=$(printf '%s%s' "$salt" "$pass" | sha256sum | cut -d' ' -f1)
    printf '%s:%s$%s\n' "$user" "$salt" "$hash"
done