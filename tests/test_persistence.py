from pathlib import Path
import subprocess
import sys
import tempfile

root=Path(sys.argv[1]).resolve()
out=Path(tempfile.mkdtemp(prefix='ps5-persistence-checks-'))
(out/'ps5').mkdir()
(out/'ps5/kernel.h').write_text('#include <stdint.h>\nuint32_t kernel_get_fw_version(void);\n')
(out/'web-test.h').write_text('static const unsigned char web[]="test page";\n')
(out/'build-id.h').write_text('#define DZ_BUILD_ID "host-persistence-test"\n')
source=r'''
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include "clock.h"
static int64_t now=1000;
static int sync_fail,fsync_calls,unlink_fail;
static int test_fsync(int fd){if(++fsync_calls==sync_fail){errno=EIO;return -1;}return fsync(fd);}
static int test_unlink(const char *p){if(unlink_fail){errno=EACCES;return -1;}return unlink(p);}
static int64_t test_milliseconds(void){return now;}
static int test_getsockname(int fd,struct sockaddr *p,socklen_t *n){
 (void)fd;struct sockaddr_in *a=(void*)p;*n=sizeof(*a);a->sin_family=AF_INET;a->sin_port=htons(8089);inet_pton(AF_INET,"127.0.0.1",&a->sin_addr);return 0;
}
#define fsync test_fsync
#define unlink test_unlink
#define dz_milliseconds test_milliseconds
#define getsockname test_getsockname
#define DZ_SAVE_PATH "SAVE_TEST_PATH"
#define WEB_HEADER "web-test.h"
#define main payload_main
#include "SERVER_PATH"
#undef main
#undef fsync
#undef unlink
static struct dz_state home={.pid=12,.started=99,.config={{{0,180,60},{0,180,60}}}};
static int home_fail,apply_fail,apply_calls,game_enabled,game_calls;
static struct dz_config game_config;
uint32_t kernel_get_fw_version(void){return 0x10200000;}
int dz_read(struct dz_state *s){*s=home;return home_fail;}
const char *dz_error(void){return "Home unavailable";}
int dz_apply(const struct dz_config *c,int enabled){++apply_calls;if(apply_fail)return -1;home.config=*c;home.enabled=enabled;return 0;}
void game_tick(const struct dz_config *c,int enabled){++game_calls;game_config=*c;game_enabled=enabled;}
int game_sync(const struct dz_config *c,int enabled){game_tick(c,enabled);return 0;}
void game_refresh(void){}
void game_read(struct game_status *s){memset(s,0,sizeof(*s));s->error="";s->enabled=game_enabled;s->config=game_config;}
static void raw(const void *p,size_t n){FILE *f=fopen(DZ_SAVE_PATH,"wb");assert(f);assert(fwrite(p,1,n,f)==n);assert(!fclose(f));}
static char response[4096];
static void command(const char *path,const char *body){
 int pair[2];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));char request[512];
 int n=snprintf(request,sizeof(request),"POST %s HTTP/1.1\r\nHost: 127.0.0.1:8089\r\nX-Deadzone-Request: 1\r\nContent-Length: %zu\r\n\r\n%s",path,strlen(body),body);
 assert(n>0 && n<(int)sizeof(request));assert(send(pair[0],request,(size_t)n,0)==n);shutdown(pair[0],SHUT_WR);serve(pair[1]);close(pair[1]);
 size_t used=0;ssize_t got;while((got=recv(pair[0],response+used,sizeof(response)-used-1,0))>0)used+=(size_t)got;
 response[used]=0;close(pair[0]);
}
int main(void){
 struct dz_config first={{{45,180,60},{12,90,30}}},second={{{22,270,90},{0,0,1}}},loaded;
 assert(!dz_save(DZ_SAVE_PATH,&first));struct stat st;assert(!stat(DZ_SAVE_PATH,&st) && (st.st_mode&0777)==0600);
 assert(!dz_load(DZ_SAVE_PATH,&loaded)&&dz_equal(&first,&loaded));
 fsync_calls=0;sync_fail=1;assert(dz_save(DZ_SAVE_PATH,&second));sync_fail=0;
 assert(!dz_load(DZ_SAVE_PATH,&loaded)&&dz_equal(&first,&loaded));
 fsync_calls=0;sync_fail=2;assert(dz_save(DZ_SAVE_PATH,&second));sync_fail=0;
 assert(!dz_load(DZ_SAVE_PATH,&loaded)&&dz_equal(&second,&loaded));
 const char embedded[]="radius=1&angle=0&width=60&rightRadius=0&rightAngle=0&rightWidth=1\0x";
 raw(embedded,sizeof(embedded)-1);assert(dz_load(DZ_SAVE_PATH,&loaded)&&errno==EINVAL);
 char large[128];memset(large,' ',sizeof(large));raw(large,sizeof(large));assert(dz_load(DZ_SAVE_PATH,&loaded));
 assert(!unlink(DZ_SAVE_PATH));assert(!symlink("missing-target",DZ_SAVE_PATH));assert(dz_load(DZ_SAVE_PATH,&loaded));
 assert(!unlink(DZ_SAVE_PATH));assert(!mkfifo(DZ_SAVE_PATH,0600));assert(dz_load(DZ_SAVE_PATH,&loaded));assert(!unlink(DZ_SAVE_PATH));
 assert(!dz_save(DZ_SAVE_PATH,&first));load_saved();assert(saved.has&&desired_enabled&&dz_equal(&desired,&first));
 follow_game();assert(home.enabled&&dz_equal(&home.config,&first)&&game_enabled&&dz_equal(&game_config,&first));
 int calls=apply_calls;home.enabled=0;++home.started;follow_game();assert(apply_calls==calls+1&&home.enabled);
 ++home.started;home.enabled=0;apply_fail=1;calls=apply_calls;follow_game();assert(apply_calls==calls+1);
 now+=4999;follow_game();assert(apply_calls==calls+1);now++;follow_game();assert(apply_calls==calls+2);
 now+=5000;follow_game();assert(apply_calls==calls+3);now+=5000;follow_game();assert(apply_calls==calls+3);
 apply_fail=0;unlink_fail=1;command("/api/disable","");assert(strstr(response,"HTTP/1.1 200")&&strstr(response,"Cannot clear saved setting")&&strstr(response,"\"saved\":false"));
 assert(!desired_enabled&&!game_enabled);++home.started;follow_game();assert(!home.enabled&&!game_enabled);
 unlink_fail=0;home_fail=1;apply_fail=1;command("/api/disable","");assert(strstr(response,"HTTP/1.1 409"));
 assert(!desired_enabled&&!game_enabled&&access(DZ_SAVE_PATH,F_OK)==-1);
 home_fail=0;apply_fail=0;++home.started;follow_game();assert(!game_enabled);
 fsync_calls=0;sync_fail=1;
 command("/api/apply","radius=22&angle=270&width=90&rightRadius=0&rightAngle=0&rightWidth=1");
 assert(strstr(response,"HTTP/1.1 200")&&strstr(response,"saving for the next boot failed")&&strstr(response,"\"saved\":false"));
 assert(home.enabled&&game_enabled);sync_fail=0;
 command("/api/apply","radius=22&angle=270&width=90&rightRadius=0&rightAngle=0&rightWidth=1");
 assert(strstr(response,"\"saved\":true")&&strstr(response,"\"saveError\":\"\""));assert(!dz_load(DZ_SAVE_PATH,&loaded)&&dz_equal(&second,&loaded));
 command("/api/disable","");assert(strstr(response,"\"saved\":true")&&!saved.has&&!home.enabled&&!game_enabled);
 raw("invalid",7);desired_known=0;load_saved();assert(!saved.has&&saved.error[0]&&!desired_known);
 puts("PASS: private atomic save, file/directory sync failures, bounded strict load, symlink/FIFO rejection, process restart, bounded retries, disable cancellation, independent game disable and visible persistence errors");
}
'''.replace('SERVER_PATH',str(root/'src/server.c')).replace('SAVE_TEST_PATH',str(out/'setting.conf'))
path=out/'persistence.c';path.write_text(source)
binary=out/'persistence'
subprocess.run(['clang','-std=gnu11','-O1','-Wall','-Wextra','-Werror','-fsanitize=undefined','-I',str(root/'src'),'-I',str(out),str(path),'-o',str(binary)],check=True)
subprocess.run([str(binary)],check=True,timeout=15)
print('Persistence test artifacts:',out)
