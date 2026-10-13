from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(sys.argv[1]).resolve()
headers = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else root / 'src'
out = Path(tempfile.mkdtemp(prefix='ps5-host-regressions-'))

def function(file, name):
    text = (root / file).read_text()
    start = text.rfind('\n', 0, text.index(name + '(')) + 1
    brace = text.index('{', start)
    end = brace + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

preamble = r'''
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "patch.h"
#include "game-code.h"
#include "filter.h"
#define PT_ATTACH 10
#define PT_DETACH 11
static int inject, writes;
static unsigned char memory_code[512];
static uintptr_t memory_slots[128];
static int ptrace(int op,pid_t p,void *a,int d){(void)op;(void)p;(void)a;(void)d;return 0;}
static pid_t test_waitpid(pid_t p,int *s,int o){(void)o;*s=(SIGSTOP<<8)|0x7f;return p;}
#define waitpid test_waitpid
static uint64_t kernel_get_ucred_authid(pid_t p){(void)p;return 1;}
static int kernel_get_ucred_caps(pid_t p,void *c){(void)p;memset(c,0,16);return 0;}
static int kernel_set_ucred_authid(pid_t p,uint64_t a){(void)p;(void)a;return 0;}
static int kernel_set_ucred_caps(pid_t p,const void *c){(void)p;(void)c;return 0;}
static int ensure(void){return 0;}
static int validate(void){return 0;}
static void *at(uintptr_t addr,size_t n){
    if(addr>=0x17e00 && addr+n<=0x17e00+sizeof(memory_code))return memory_code+addr-0x17e00;
    assert(addr>=0x40000 && addr+n<=0x40000+sizeof(memory_slots));
    return (unsigned char*)memory_slots+addr-0x40000;
}
static int remote_read(uintptr_t addr,void *data,size_t n){memcpy(data,at(addr,n),n);return 0;}
int dz_write_remote(int pid,uintptr_t addr,const void *data,size_t n){
    (void)pid;++writes;
    if(writes==inject){memcpy(at(addr,n),data,n/2);return -1;}
    memcpy(at(addr,n),data,n);return 0;
}
'''

cases = {}
for side in ['home', 'game']:
    if side == 'home':
        before = r'''
static struct {int pid,count,enabled;uintptr_t pad,cave,slots[6],funcs[5];int kinds[6];struct dz_config config;} target;
static const char *error;
static int fail(const char *s){error=s;return -1;}
static int read_remote(int p,uintptr_t a,void *d,size_t n){(void)p;return remote_read(a,d,n);}
int dz_read_remote(int p,uintptr_t a,void *d,size_t n){return read_remote(p,a,d,n);}
static int verify(int p,uintptr_t a,const void *d,size_t n){(void)p;return memcmp(at(a,n),d,n)?-1:0;}
'''
        body = function('src/patch.c', 'dz_apply')
        after = r'''
int main(void){
 target.pid=1;target.count=6;target.cave=0x17e00;
 for(int i=0;i<6;i++){target.slots[i]=0x40000+i*8;target.kinds[i]=i%2;target.funcs[i%5]=0;}
 target.funcs[0]=0x2960;target.funcs[4]=0x29a0;
 for(int failure=1;failure<=7;failure++){
  memset(memory_code,0,sizeof(memory_code));
  for(int i=0;i<6;i++)memory_slots[i]=target.kinds[i]?target.funcs[4]:target.funcs[0];
  uintptr_t before[6];memcpy(before,memory_slots,sizeof(before));
  writes=0;inject=failure;
  assert(dz_apply(&dz_default,1)==-1);assert(writes>0);
  assert(!memcmp(before,memory_slots,sizeof(before)));
  for(size_t i=0;i<sizeof(blob);i++)assert(memory_code[i]==0);
 }
 puts("PASS: home first-install rollback, seven injected partial write failures");
}
'''
    else:
        before = r'''
#define MAX_IMPORTS 128
static struct {int pid,count,enabled;uintptr_t pad,cave,funcs[5],slots[MAX_IMPORTS];unsigned char kinds[MAX_IMPORTS];struct dz_config config;const char *error;} game;
static int fail(const char *s){game.error=s;return -1;}
static int read_at(uintptr_t a,void *d,size_t n){return remote_read(a,d,n);}
static int checked_write(uintptr_t a,const void *d,size_t n){return dz_write_remote(game.pid,a,d,n)||memcmp(at(a,n),d,n)?-1:0;}
'''
        body = function('src/game.c', 'apply')
        after = r'''
int main(void){
 game.pid=1;game.count=5;game.cave=0x17e00;
 for(int i=0;i<5;i++){game.slots[i]=0x40000+i*8;game.kinds[i]=i;game.funcs[i]=0x2960+16*(size_t)i;}
 for(int failure=1;failure<=6;failure++){
  memset(memory_code,0,sizeof(memory_code));
  for(int i=0;i<5;i++)memory_slots[i]=game.funcs[i];
  uintptr_t before[5];memcpy(before,memory_slots,sizeof(before));
  writes=0;inject=failure;
  assert(apply(&dz_default,1)==-1);assert(writes>0);
  assert(!memcmp(before,memory_slots,sizeof(before)));
  for(size_t i=0;i<sizeof(game_blob);i++)assert(memory_code[i]==0);
 }
 puts("PASS: game first-install rollback, six injected partial write failures");
}
'''
    cases[side + '_rollback'] = preamble + before + body + after

read_name = 'dz_read_remote' if 'int dz_read_remote(' in (root / 'src/patch.c').read_text() else 'read_remote'
cases['page_read'] = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
static unsigned calls;
static int kernel_proc_copyout(pid_t p,intptr_t a,void *b,size_t n){
 (void)p;++calls;assert(n<=4096-((uintptr_t)a&4095));memset(b,0x5a,n);return 0;
}
''' + function('src/patch.c', read_name) + r'''
int main(void){unsigned char b[0x8ae2];memset(b,0,sizeof(b));
 assert(!READ_FN(1,0x20018,b+1,0x8ae0));assert(b[0]==0&&b[sizeof(b)-1]==0);
 for(unsigned i=1;i<sizeof(b)-1;i++)assert(b[i]==0x5a);
 assert(calls==9);puts("PASS: remote read keeps every SDK call within one 4 KiB page");}
'''.replace('READ_FN', read_name)

(out / 'ps5').mkdir()
(out / 'ps5/kernel.h').write_text('#include <stdint.h>\nuint32_t kernel_get_fw_version(void);\n')
(out / 'build-id.h').write_text('#define DZ_BUILD_ID \"host-test\"\n')
(out / 'web-test.h').write_text('static const unsigned char web[]="test page";\n')
server = r'''
#include <arpa/inet.h>
#include <assert.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>
#include <time.h>
static int test_getsockname(int fd,struct sockaddr *p,socklen_t *n){
 (void)fd;struct sockaddr_in *a=(void*)p;*n=sizeof(*a);a->sin_family=AF_INET;a->sin_port=htons(8089);inet_pton(AF_INET,"127.0.0.1",&a->sin_addr);return 0;
}
#define getsockname test_getsockname
#define main payload_main
#define DZ_SAVE_PATH "SAVE_TEST_PATH"
#define WEB_HEADER "web-test.h"
#include "SERVER_PATH"
#undef main
static int home_fail, sync_calls, tick_calls, refresh_calls, tick_enabled;
uint32_t kernel_get_fw_version(void){return 0x10200000;}
int dz_read(struct dz_state *s){memset(s,0,sizeof(*s));s->config=dz_default;s->enabled=1;return home_fail;}
const char *dz_error(void){return "Home unavailable";}
int dz_apply(const struct dz_config *c,int e){(void)c;(void)e;return home_fail;}
void game_read(struct game_status *s){memset(s,0,sizeof(*s));s->error="";s->pid=42;s->config=dz_default;s->input.fresh=1;s->input.connected=1;s->input.lx=155;}
int game_sync(const struct dz_config *c,int e){(void)c;(void)e;++sync_calls;return 0;}
void game_tick(const struct dz_config *c,int e){(void)c;++tick_calls;tick_enabled=e;}
void game_refresh(void){++refresh_calls;}
int main(int argc,char **argv){
 assert(argc==2);signal(SIGPIPE,SIG_IGN);int pair[2];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));
 if(!strcmp(argv[1],"follow")) {
  home_fail=1;follow_game();assert(refresh_calls==1 && tick_calls==0);
  home_fail=0;follow_game();assert(tick_calls==1 && tick_enabled==1);
  home_fail=1;follow_game();assert(tick_calls==2 && tick_enabled==1);
  puts("PASS: game monitoring continues when home is unavailable");return 0;
 }
 if(!strcmp(argv[1],"disable-home") || !strcmp(argv[1],"state-home"))home_fail=1;
 pid_t child=fork();assert(child>=0);
 if(!child){close(pair[0]);struct timeval t={2,0};setsockopt(pair[1],SOL_SOCKET,SO_RCVTIMEO,&t,sizeof(t));serve(pair[1]);if(!strcmp(argv[1],"disable-home")){assert(sync_calls==1);home_fail=0;follow_game();assert(tick_calls==1 && tick_enabled==0);}close(pair[1]);_exit(0);}
 close(pair[1]);
 if(!strcmp(argv[1],"slow")){
  struct timespec start,now;clock_gettime(CLOCK_MONOTONIC,&start);int status;
  for(int i=0;i<8;i++){
   send(pair[0],"G",1,0);usleep(400000);
   if(waitpid(child,&status,WNOHANG)==child){clock_gettime(CLOCK_MONOTONIC,&now);double elapsed=now.tv_sec-start.tv_sec+(now.tv_nsec-start.tv_nsec)/1e9;assert(elapsed<2.8);puts("PASS: trickle client is closed by absolute deadline");return 0;}
  }
  shutdown(pair[0],SHUT_WR);waitpid(child,&status,0);assert(!"trickle client blocked serve beyond deadline");
 }
 const char *request=!strcmp(argv[1],"state-home")?"GET /api/state HTTP/1.1\r\nHost: 127.0.0.1:8089\r\n\r\n":home_fail?"POST /api/disable HTTP/1.1\r\nHost: 127.0.0.1:8089\r\nX-Deadzone-Request: 1\r\nContent-Length: 0\r\n\r\n":!strcmp(argv[1],"host")?"GET / HTTP/1.1\r\nHost: attacker.invalid:8089\r\n\r\n":
  !strcmp(argv[1],"origin")?"POST /api/close HTTP/1.1\r\nHost: 127.0.0.1:8089\r\nOrigin: http://attacker.invalid\r\nX-Deadzone-Request: 1\r\nContent-Length: 0\r\n\r\n":
  "GET / HTTP/1.1\r\nHost: 127.0.0.1:8089\r\n\r\n";
 send(pair[0],request,strlen(request),0);shutdown(pair[0],SHUT_WR);
 char response[2048];size_t used=0;ssize_t got;
 while((got=recv(pair[0],response+used,sizeof(response)-used-1,0))>0)used+=got;
 response[used]=0;int status;waitpid(child,&status,0);assert(WIFEXITED(status)&&WEXITSTATUS(status)==0);
 assert(strstr(response,!strcmp(argv[1],"disable-home")?"HTTP/1.1 409":(!strcmp(argv[1],"valid")||!strcmp(argv[1],"state-home"))?"HTTP/1.1 200":"HTTP/1.1 403"));
 if(!strcmp(argv[1],"state-home")) {
  assert(strstr(response,"\"homeAvailable\":false") && strstr(response,"\"source\":\"game\""));
  assert(strstr(response,"Home unavailable") && strstr(response,"\"gamePid\":42") && strstr(response,"\"buildId\":"));
 }
 printf("PASS: HTTP %s case\n",argv[1]);
}
'''.replace('SERVER_PATH', str(root / 'src/server.c')).replace('SAVE_TEST_PATH',str(out/'setting.conf'))
cases['http_server'] = server

failures = 0
for name, source in cases.items():
    path = out / (name + '.c')
    path.write_text(source)
    binary = out / name
    subprocess.run(['clang', '-std=gnu11', '-O1', '-fsanitize=undefined', '-I', str(root / 'src'), '-I', str(headers), '-I', str(out), str(path), '-o', str(binary)], check=True)
    for args in ([['valid'], ['host'], ['origin'], ['slow'], ['disable-home'], ['state-home'], ['follow']] if name == 'http_server' else [[]]):
        done = subprocess.run([str(binary)] + args, capture_output=True, text=True, timeout=15)
        print(done.stdout.strip())
        if done.returncode:
            failures += 1
            print('FAIL:', name, *args, done.stderr.strip().splitlines()[-1:])
print('Host fault-injection failures:', failures, '; artifacts:', out)
sys.exit(bool(failures))
