#ifndef _SYS_UN_H
#define _SYS_UN_H
#include <stddef.h>
#include <string.h>
#include <sys/socket.h>

struct sockaddr_un {
    sa_family_t sun_family;
    char        sun_path[108];
};

#define SUN_LEN(p) (offsetof(struct sockaddr_un, sun_path) + strlen((p)->sun_path))
#endif