"""Keep LCD's ISR pointer table internal even with ordinary malloc in PSRAM."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
DISPLAY = ROOT / 'firmware/rickyos-plus-1.2/components/metalio_board/display'
with tempfile.TemporaryDirectory(prefix='plus-ble-memory-') as folder:
    p = Path(folder)
    (p / 'line_queue.c').write_bytes((DISPLAY / 'output_common/line_queue.c').read_bytes())
    (p / 'line_queue.h').write_bytes((DISPLAY / 'output_common/line_queue.h').read_bytes())
    (p / 'render_method.h').write_text('#pragma once\n')
    (p / 'esp_log.h').write_text('#pragma once\nstatic inline void host_log(const char* tag,const char* fmt,...){(void)tag;(void)fmt;}\n#define ESP_LOGD(...) host_log(__VA_ARGS__)\n')
    (p / 'esp_attr.h').write_text('#pragma once\n#define IRAM_ATTR\n')
    (p / 'esp_heap_caps.h').write_text('''#pragma once
#include <stdlib.h>
#include <assert.h>
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
extern int attempts, fail_at, live;
static inline void* heap_caps_calloc(size_t n,size_t size,int caps){
 assert(caps==3 && n==64 && size==sizeof(void*));
 if(++attempts==fail_at)return NULL;
 return calloc(n,size); // ASan checks table lifetime; live counts row payloads.
}
static inline void* heap_caps_aligned_alloc(size_t align,size_t size,int caps){
 assert(align==16 && size==16 && caps==1);
 if(++attempts==fail_at)return NULL;
 void* p=NULL;if(posix_memalign(&p,align,size))return NULL;++live;return p;
}
static inline void heap_caps_free(void* p){if(p)--live;free(p);}
''')
    (p / 'test.c').write_text('''#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include "line_queue.h"
int attempts=0,fail_at=0,live=0;
int main(int argc,char** argv){
 for(int failure=argc>1?atoi(argv[1]):0;failure<=(argc>1?atoi(argv[1]):0);++failure){
  attempts=0;fail_at=failure;LineQueue_t q=lq_init(64,7);
  if(failure){assert(q.bufs==NULL&&live==0);continue;}
  assert(q.bufs&&live==64&&q.size==64&&q.element_size==7);
  unsigned char* row=lq_current(&q);assert(row);memset(row,42,7);lq_commit(&q);
  unsigned char out[7];assert(lq_read(&q,out)==0&&out[0]==42&&out[6]==42);
  lq_free(&q);assert(live==0);
 }
}
''')
    subprocess.run(['clang', '-std=c11', '-D_POSIX_C_SOURCE=200112L', '-DFREEINK_DEVICE_METALIO_PLUS=1',
                    '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', '-I', str(p),
                    str(p / 'test.c'), str(p / 'line_queue.c'), '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)
    import signal
    for failure in range(1,66):
        result = subprocess.run([str(p / 'test'),str(failure)],capture_output=True)
        assert result.returncode == -signal.SIGABRT and b'Assertion' in result.stderr,(failure,result.stderr)
print('Plus BLE memory: real LCD queue geometry, internal pointer/row allocations and all 65 existing boot-OOM assertions passed')
