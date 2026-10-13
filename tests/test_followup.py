from pathlib import Path
import subprocess
import sys
import tempfile

root=Path(sys.argv[1]).resolve()
out=Path(tempfile.mkdtemp(prefix='ps5-followup-checks-'))

def function(file,name):
    text=(root/file).read_text()
    start=text.rfind('\n',0,text.index(name+'('))+1
    brace=text.index('{',start)
    end=brace+1
    depth=1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}')
        end+=1
    return text[start:end]

cases={}
cases['input_clock']=r"""
#include <assert.h>
#include <stdio.h>
#include "input.h"
int main(void) {
 struct dz_state state={0};struct dz_observation seen={0};
 unsigned char sample[168]={0};sample[4]=sample[5]=sample[6]=sample[7]=128;sample[76]=1;
 assert(!dz_sample(&state,&seen,sample,100,0));assert(state.connected&&!state.fresh);
 assert(!dz_sample(&state,&seen,sample,100,1000));assert(!state.fresh);
 assert(!dz_sample(&state,&seen,sample,101,1100));assert(state.fresh);
 assert(!dz_sample(&state,&seen,sample,101,3100));assert(!state.fresh);
 assert(!dz_sample(&state,&seen,sample,102,6000));assert(state.fresh&&state.idle);
 sample[4]=140;assert(!dz_sample(&state,&seen,sample,103,6010));assert(!state.idle);
 assert(!dz_sample(&state,&seen,NULL,0,6020));assert(!state.connected&&!state.fresh&&state.timestamp==0&&state.lx==128);
 assert(!dz_sample(&state,&seen,sample,104,6030));assert(!state.fresh);
 assert(!dz_sample(&state,&seen,sample,105,6040));assert(state.fresh);
 assert(!dz_sample(&state,&seen,sample,90,6050));assert(!state.fresh);
 assert(dz_sample(&state,&seen,sample,91,-1)==-1);assert(!state.fresh&&!state.connected);
 puts("PASS: first sample, stale input, disconnect, timestamp reset, idle and clock failure");
}
"""
cases['backoff']=r"""
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "patch.h"
static int calls,failures=1;static int64_t now;
static int64_t dz_milliseconds(void){return now;}
static int fail(const char *s){(void)s;return -1;}
static int apply(const struct dz_config *c,int e){(void)c;(void)e;++calls;return failures?-1:0;}
"""+function('src/game.c','game_tick')+r"""
int main(void) {
 now=100;game_tick(&dz_default,1);assert(calls==1);
 now=1099;game_tick(&dz_default,1);assert(calls==1);
 now=5099;game_tick(&dz_default,1);assert(calls==1);
 now=5100;failures=0;game_tick(&dz_default,1);assert(calls==2);
 now=5101;game_tick(&dz_default,1);assert(calls==3);
 now=-1;game_tick(&dz_default,1);assert(calls==3);
 puts("PASS: automatic patch failures back off and clock failure prevents patching");
}
"""
cases['home_identity']=r"""
#include <assert.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/time.h>
#include "input.h"
static struct {pid_t pid;int count;struct timeval started;uintptr_t slots[6];} target;
static struct dz_observation observation;
static int located,validated;static struct timeval current={10,1};
static pid_t shell_pid(struct timeval *s){*s=current;return 92;}
static int fail(const char *s){(void)s;return -1;}
static int locate(pid_t pid){++located;target.pid=pid;target.count=6;target.slots[5]=1;return 0;}
static int validate(void){++validated;return 0;}
"""+function('src/patch.c','ensure')+r"""
int main(void) {
 assert(!ensure());assert(located==1&&validated==0);
 assert(!ensure());assert(located==1&&validated==1);
 observation.observed=1;current.tv_usec=2;
 assert(!ensure());assert(located==2&&validated==1&&!observation.observed);
 puts("PASS: reused home PID with a new start time forces rediscovery");
}
"""
for name,source in cases.items():
    path=out/(name+'.c');path.write_text(source)
    binary=out/name
    subprocess.run(['clang','-std=gnu11','-O1','-Wall','-Wextra','-Werror','-fsanitize=undefined','-I',str(root/'src'),str(path),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True,timeout=15)
print('Follow-up checks passed; evidence:',out)
