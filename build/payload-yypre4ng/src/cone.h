#ifndef CONE_H
#define CONE_H
#include <math.h>
#include <stdint.h>
#include <string.h>
struct cone_config { int radius, angle, width; };
/* stick[0] = left (pad bytes 4,5), stick[1] = right (pad bytes 6,7) */
struct dz_config { struct cone_config stick[2]; };
#define DZ_FIELDS 11
#define DZ_CONFIG_SIZE 24
static const struct dz_config dz_default={{{0,180,60},{0,180,60}}};
static inline int cone_valid(int radius,int angle,int width) {
    return radius>=0 && radius<=182 && angle>=0 && angle<360 && ((width>=1 && width<=180) || width==360);
}
static inline int dz_valid(const struct dz_config *c) {
    for(int s=0;s<2;s++)if(!cone_valid(c->stick[s].radius,c->stick[s].angle,c->stick[s].width))return 0;
    return 1;
}
static inline int dz_equal(const struct dz_config *a,const struct dz_config *b) {
    for(int s=0;s<2;s++)
        if(a->stick[s].radius!=b->stick[s].radius || a->stick[s].angle!=b->stick[s].angle || a->stick[s].width!=b->stick[s].width)return 0;
    return 1;
}
static inline void cone_values(int32_t values[5],int radius,int angle,int width) {
    if(width==360){values[0]=radius*radius;for(int i=1;i<5;i++)values[i]=0;return;}
    double a=(angle-width/2.0)*3.14159265358979323846/180;
    double b=(angle+width/2.0)*3.14159265358979323846/180;
    values[0]=radius*radius;
    values[1]=(int32_t)lround(-sin(a)*1024);values[2]=(int32_t)lround(cos(a)*1024);
    values[3]=(int32_t)lround(sin(b)*1024);values[4]=(int32_t)lround(-cos(b)*1024);
}
static inline void cone_patch(unsigned char *out,const unsigned fields[DZ_FIELDS],const struct dz_config *c) {
    int32_t config[6];
    for(int s=0;s<2;s++) {
        const struct cone_config *k=&c->stick[s];
        int32_t values[5];cone_values(values,k->radius,k->angle,k->width);
        for(int i=0;i<5;i++)memcpy(out+fields[s*5+i],values+i,4);
        config[s*3]=k->radius;config[s*3+1]=k->angle;config[s*3+2]=k->width;
    }
    memcpy(out+fields[10],config,DZ_CONFIG_SIZE);
}
static inline int cone_config_read(const unsigned char *code,unsigned offset,struct dz_config *out) {
    int32_t config[6];memcpy(config,code+offset,DZ_CONFIG_SIZE);
    struct dz_config c;
    for(int s=0;s<2;s++)c.stick[s]=(struct cone_config){config[s*3],config[s*3+1],config[s*3+2]};
    if(!dz_valid(&c))return -1;
    *out=c;return 0;
}
#endif
