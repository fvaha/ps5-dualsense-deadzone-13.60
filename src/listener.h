#ifndef DZ_LISTENER_H
#define DZ_LISTENER_H
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
static inline int dz_listener(unsigned short port) {
    int fd=socket(AF_INET,SOCK_STREAM,0),reuse=1;
    if (fd<0) return -1;
    struct sockaddr_in address={0};
    address.sin_family=AF_INET;address.sin_port=htons(port);address.sin_addr.s_addr=htonl(INADDR_ANY);
    if (setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse)) ||
        bind(fd,(struct sockaddr*)&address,sizeof(address)) || listen(fd,8)) {
        int saved=errno;close(fd);errno=saved;return -1;
    }
    return fd;
}
#endif
