#ifndef HTTP_CONE_H
#define HTTP_CONE_H
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include "http.h"
#include "cone.h"
#ifndef DZ_SAVE_PATH
#define DZ_SAVE_PATH "/data/ps5-dualsense-deadzone.conf"
#endif
static inline int parse_cone(const char *body,struct dz_config *config) {
    const char *keys[]={"radius=","&angle=","&width=","&rightRadius=","&rightAngle=","&rightWidth="};int values[6];
    for(int i=0;i<6;i++) {
        size_t n=strlen(keys[i]);if(strncmp(body,keys[i],n))return -1;body+=n;
        int value=0,count=0;
        while(*body>='0' && *body<='9') {
            if(++count>3)return -1;
            value=value*10+(*body++-'0');
        }
        if(!count)return -1;values[i]=value;
    }
    struct dz_config c={{{values[0],values[1],values[2]},{values[3],values[4],values[5]}}};
    if(*body || !dz_valid(&c))return -1;
    *config=c;return 0;
}
static inline int dz_sync_parent(const char *path) {
    char parent[256];
    int n=snprintf(parent,sizeof(parent),"%s",path);
    if(n<0 || (size_t)n>=sizeof(parent)){errno=ENAMETOOLONG;return -1;}
    char *slash=strrchr(parent,'/');
    if(!slash) strcpy(parent,".");
    else if(slash==parent) parent[1]=0;
    else *slash=0;
    int fd=open(parent,O_RDONLY|O_DIRECTORY);
    if(fd<0)return -1;
    int bad=fsync(fd),reason=errno;
    if(close(fd) && !bad)return -1;
    if(bad)errno=reason;
    return bad;
}
static inline int dz_save(const char *path,const struct dz_config *c) {
    char body[128],tmp[256];
    int n=snprintf(body,sizeof(body),"radius=%d&angle=%d&width=%d&rightRadius=%d&rightAngle=%d&rightWidth=%d",
        c->stick[0].radius,c->stick[0].angle,c->stick[0].width,c->stick[1].radius,c->stick[1].angle,c->stick[1].width);
    if(!dz_valid(c) || n<=0 || (size_t)n>=sizeof(body)){errno=EINVAL;return -1;}
    int size=snprintf(tmp,sizeof(tmp),"%s.XXXXXX",path);
    if(size<0 || (size_t)size>=sizeof(tmp)){errno=ENAMETOOLONG;return -1;}
    int fd=mkstemp(tmp);
    if(fd<0)return -1;
    int bad=0,reason=0;
    for(int used=0;used<n;) {
        ssize_t count=write(fd,body+used,(size_t)(n-used));
        if(count<0 && errno==EINTR)continue;
        if(count<=0){bad=1;reason=count<0?errno:EIO;break;}
        used+=(int)count;
    }
    if(!bad && fsync(fd)){bad=1;reason=errno;}
    if(close(fd) && !bad){bad=1;reason=errno;}
    if(!bad && rename(tmp,path)){bad=1;reason=errno;}
    if(bad){unlink(tmp);errno=reason;return -1;}
    return dz_sync_parent(path);
}
static inline int dz_load(const char *path,struct dz_config *c) {
    char body[128];
    int fd=open(path,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
    if(fd<0)return -1;
    struct stat info;
    if(fstat(fd,&info)){int reason=errno;close(fd);errno=reason;return -1;}
    if(!S_ISREG(info.st_mode) || info.st_size<=0 || info.st_size>=(off_t)sizeof(body)) {
        close(fd);errno=EINVAL;return -1;
    }
    size_t used=0;
    while(used<sizeof(body)) {
        ssize_t n=read(fd,body+used,sizeof(body)-used);
        if(n<0 && errno==EINTR)continue;
        if(n<0){int reason=errno;close(fd);errno=reason;return -1;}
        if(!n)break;
        used+=(size_t)n;
    }
    if(close(fd))return -1;
    if(!used || used>=sizeof(body) || memchr(body,0,used)){errno=EINVAL;return -1;}
    body[used]=0;
    if(parse_cone(body,c)){errno=EINVAL;return -1;}
    return 0;
}
static inline int dz_forget(const char *path) {
    if(unlink(path) && errno!=ENOENT)return -1;
    return dz_sync_parent(path);
}
#endif
