#include <assert.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>
#include "game-code.h"
#include "filter.h"
#include "http-cone.h"
static double wrap(double x) { double tau=6.2831853071795864769;return fmod(x+2*tau,tau); }
static int expected(int x,int y,int radius,int angle,int width) {
    if(x*x+y*y>radius*radius)return 0;
    if(width==360 || (!x && !y))return 1;
    int32_t v[5];cone_values(v,radius,angle,width);
    double start=atan2(-v[1],v[2]),end=atan2(v[3],-v[4]);
    double at=wrap(atan2(y,x)-start),span=wrap(end-start);
    return at<=span+1e-10 || at>6.2831853071795864769-1e-10;
}
int main(void) {
    struct dz_config cfg;
    assert(!parse_cone("radius=182&angle=180&width=60&rightRadius=0&rightAngle=180&rightWidth=60",&cfg));
    assert(cfg.stick[0].radius==182 && cfg.stick[1].radius==0 && cfg.stick[1].width==60);
    assert(!parse_cone("radius=182&angle=359&width=360&rightRadius=182&rightAngle=359&rightWidth=360",&cfg));
    assert(cfg.stick[1].radius==182 && cfg.stick[1].angle==359 && cfg.stick[1].width==360);assert(expected(127,0,182,180,360));assert(expected(0,-128,182,180,360));
    #define R "&rightRadius=0&rightAngle=180&rightWidth=60"
    const char *bad[]={"radius=183&angle=180&width=60" R,"radius=182&angle=360&width=60" R,"radius=182&angle=180&width=0" R,"radius=182&angle=180&width=181" R,"radius=-1&angle=180&width=60" R,"radius=1&angle=0&width=1x" R,"radius=1&angle=0&width=1&width=2" R,"radius=1&angle=&width=60" R,"radius=1&width=60&angle=180" R,"radius=1&angle=0000&width=60" R,
        "radius=182&angle=180&width=60","radius=182&angle=180&width=60&rightRadius=183&rightAngle=180&rightWidth=60","radius=182&angle=180&width=60&rightRadius=0&rightAngle=360&rightWidth=60","radius=182&angle=180&width=60&rightRadius=0&rightAngle=180&rightWidth=181","radius=182&angle=180&width=60&rightRadius=0&rightAngle=180&rightWidth=60&","radius=182&angle=180&width=60&rightAngle=180&rightRadius=0&rightWidth=60"};
#undef R
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++)assert(parse_cone(bad[i],&cfg));
    {
        char path[]="/tmp/dz-save-XXXXXX";int fd=mkstemp(path);assert(fd>=0);close(fd);
        struct dz_config saved={{{182,359,360},{0,0,1}}},loaded;
        assert(!dz_save(path,&saved) && !dz_load(path,&loaded) && dz_equal(&saved,&loaded));
        struct dz_config invalid={{{183,0,60},{0,0,1}}};assert(dz_save(path,&invalid) && !dz_load(path,&loaded) && dz_equal(&saved,&loaded));
        FILE *f=fopen(path,"w");fputs("radius=1&angle=0&width=60&rightRadius=0&rightAngle=0&rightWidth=1x",f);fclose(f);assert(dz_load(path,&loaded));
        f=fopen(path,"w");const char embedded[]="radius=1&angle=0&width=60&rightRadius=0&rightAngle=0&rightWidth=1\0x";fwrite(embedded,1,sizeof(embedded)-1,f);fclose(f);assert(dz_load(path,&loaded));
        assert(!dz_forget(path) && dz_load(path,&loaded) && !dz_forget(path));
    }
    assert(expected(-128,4,182,180,60));
    assert(!expected(127,0,182,180,60));assert(!expected(0,-128,182,180,60));assert(!expected(0,127,182,180,60));
    assert(!expected(-100,0,64,180,60));assert(expected(-64,0,64,180,60));
    size_t page=(size_t)sysconf(_SC_PAGESIZE);
    unsigned char *code=mmap(NULL,page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);assert(code!=MAP_FAILED && page>2048);
    uintptr_t functions[5];for(int i=0;i<5;i++)functions[i]=(uintptr_t)(code+1024);
    const unsigned char stub[]={0x89,0xf8,0xc3};
    int angles[]={0,30,90,179,180,225,270,359},widths[]={1,30,60,90,120,179,180,360},radii[]={0,64,128,182};
    /* every config runs on the left stick; the right stick gets another config from the same set (c*97+41 permutes 0..255),
       and its XY is a permutation of the left XY, so both sticks sweep all 65536 positions in the same packets */
    unsigned long tested=0;static unsigned char wanted[2][65536];
    for(unsigned c=0;c<256;c++) {
        struct dz_config k;
        for(int s=0;s<2;s++){unsigned i=s?(c*97+41)&255:c;k.stick[s]=(struct cone_config){radii[i&3],angles[i>>5],widths[(i>>2)&7]};}
        for(int s=0;s<2;s++)for(unsigned xy=0;xy<65536;xy++)wanted[s][xy]=(unsigned char)expected((xy&255)-128,(xy>>8)-128,k.stick[s].radius,k.stick[s].angle,k.stick[s].width);
        for(int home=0;home<2;home++) {
            assert(!mprotect(code,page,PROT_READ|PROT_WRITE));memcpy(code+1024,stub,sizeof(stub));
            if(home)dz_code(code,functions[0],functions[0],&k);
            else assert(!game_build(code,(uintptr_t)code,functions,&k));
            assert(!mprotect(code,page,PROT_READ|PROT_EXEC));
            for(int k=0;k<(home?2:5);k++) {
                int (*fn)(int,void*,int)=(void*)(code+(home?home_entries[k]:game_entries[k]));
                int multi=!home && (k==1 || k==3);unsigned stride=home?(k?168:120):(k==4?168:k>=2?128:120);
                assert(fn(-1,NULL,1)==-1 && fn(0,NULL,1)==0);
                for(unsigned xy=0;xy<65536;xy++) {
                    unsigned rxy=(xy*40503u+12345u)&65535;
                    unsigned char data[208],want[208];memset(data,0xa5,sizeof(data));data[20]=xy&255;data[21]=xy>>8;data[22]=rxy&255;data[23]=rxy>>8;data[92]=1;memcpy(want,data,sizeof(data));
                    if(wanted[0][xy])want[20]=want[21]=128;
                    if(wanted[1][rxy])want[22]=want[23]=128;
                    assert(fn(multi?1:0,data+16,1)==(multi?1:0));assert(!memcmp(data,want,sizeof(data)));tested++;
                }
                for(int n=1;n<=64;n++) {
                    unsigned char data[64*168+32],want[sizeof(data)];memset(data,0xa5,sizeof(data));
                    for(int i=0;i<n;i++){unsigned o=16+i*stride;data[o+4]=(unsigned char)(i*17);data[o+5]=(unsigned char)(i*29);data[o+6]=(unsigned char)(i*53);data[o+7]=(unsigned char)(i*71);data[o+76]=i%2;}
                    memcpy(want,data,sizeof(data));
                    for(int i=0;i<(multi?n:1);i++){unsigned o=16+i*stride;if(i%2 && wanted[0][data[o+4]+256*data[o+5]])want[o+4]=want[o+5]=128;if(i%2 && wanted[1][data[o+6]+256*data[o+7]])want[o+6]=want[o+7]=128;}
                    assert(fn(multi?n:0,data+16,n)==(multi?n:0));assert(!memcmp(data,want,sizeof(data)));
                }
                unsigned char data[208],want[208];memset(data,0xa5,sizeof(data));data[92]=1;memcpy(want,data,sizeof(data));
                assert(fn(-1,data+16,1)==-1 && !memcmp(data,want,sizeof(data)));
                if(multi){assert(fn(2,data+16,1)==2 && !memcmp(data,want,sizeof(data)));assert(fn(65,data+16,65)==65 && !memcmp(data,want,sizeof(data)));assert(fn(0,data+16,1)==0 && !memcmp(data,want,sizeof(data)));}
                else assert(fn(1,data+16,1)==1 && !memcmp(data,want,sizeof(data)));
            }
        }
    }
    unsigned char sample[sizeof(game_blob)];uintptr_t pad=0x800000000ull;
    for(int i=0;i<5;i++)functions[i]=pad+0x2960+16*(size_t)i;
    struct dz_config both={{{182,180,60},{90,45,120}}};
    assert(!game_build(sample,pad+0x17e00,functions,&both));assert(!game_code_config(sample,pad+0x17e00,functions,&cfg));assert(dz_equal(&cfg,&both));
    {uintptr_t moved[5];for(int i=0;i<5;i++)moved[i]=functions[i]+0x4000;
    assert(!game_code_config(sample,pad+0x4000+0x17e00,moved,&cfg));}
    sample[40]^=1;assert(game_code_config(sample,pad+0x17e00,functions,&cfg));
    unsigned char homecode[sizeof(blob)];dz_code(homecode,functions[0],functions[4],&both);assert(!dz_code_config(homecode,functions[0],functions[4],&cfg));assert(dz_equal(&cfg,&both));homecode[100]^=1;assert(dz_code_config(homecode,functions[0],functions[4],&cfg));
    assert(!munmap(code,page));printf("PASS: %lu XY assembly cases (left and right stick in every packet), seven entry points, angular edges, batches 1..64, buffer guards, errors, config validation, form parser and saved-setting file\n",tested);
}
