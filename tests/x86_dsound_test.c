#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../runtime/shim-port/elf/com_dsound.h"
#include "../runtime/shim-port/elf/host_elf.h"

#if defined(__i386__)
#define S __attribute__((stdcall))
#else
#define S
#endif

static int audio_open, opens, closes, used, ticks;
static host_audio_voice voices[HOST_VOICES];
static unsigned long positions[HOST_VOICES];
static unsigned int playing[HOST_VOICES];

void reasm_elf_log(const char *s) { (void)s; }
void reasm_elf_log_hex(unsigned int n) { (void)n; }
unsigned int host_elf_ticks(void) { return (unsigned int)++ticks; }
int host_elf_audio_open(int rate, int channels, int bits) { (void)rate;(void)channels;(void)bits; audio_open=1; ++opens; return 0; }
void host_elf_audio_close(void) { audio_open=0; ++closes; }
int host_elf_audio_is_open(void) { return audio_open; }
unsigned long host_elf_audio_bytes(void) { return 0; }
int host_elf_voice_used_count(void) { return used; }
int host_elf_voice_open(const host_audio_voice *v) {
    int i; for (i=0;i<HOST_VOICES;i++) if (!playing[i]) { voices[i]=*v; positions[i]=0; playing[i]=1; ++used; return i; }
    return -1;
}
void host_elf_voice_close(int id) { if (id>=0 && id<HOST_VOICES && playing[id]) { playing[id]=0; --used; } }
void host_elf_voice_set(int id, const host_audio_voice *v) { if(id>=0&&id<HOST_VOICES) voices[id]=*v; }
void host_elf_voice_set_pos(int id, unsigned long p) { if(id>=0&&id<HOST_VOICES) positions[id]=p; }
unsigned long host_elf_voice_pos(int id) { return id>=0&&id<HOST_VOICES?positions[id]:0; }
unsigned int host_elf_voice_status(int id) { return id>=0&&id<HOST_VOICES&&playing[id]?HOST_VOICE_PLAYING:0; }
void host_elf_voice_play(int id) { if(id>=0&&id<HOST_VOICES) playing[id]=1; }
void host_elf_voice_stop(int id) { if(id>=0&&id<HOST_VOICES) playing[id]=0; }

/* Vtable wrappers are identity functions in this isolated C test (no guest ABI bridge). */
void arm_com_wrap_vtable(void **vt, const unsigned char *args, unsigned n) { (void)vt;(void)args;(void)n; }
void *arm_com_thunk(void *fn, unsigned a, unsigned r) { (void)a;(void)r; return fn; }

#define CALL(type, obj, slot) ((type)((void ***)(obj))[0][slot])
typedef long (S *create_fn)(const void *, void **, void *);
typedef long (S *coop_fn)(void *, void *, unsigned int);
typedef long (S *bufcreate_fn)(void *, void *, void **, void *);
typedef long (S *duplicate_fn)(void *, void *, void **);
typedef long (S *lock_fn)(void *, unsigned int, unsigned int, void **, unsigned int *, void **, unsigned int *, unsigned int);
typedef long (S *unlock_fn)(void *, void *, unsigned int, void *, unsigned int);
typedef long (S *play_fn)(void *, unsigned int, unsigned int, unsigned int);
typedef long (S *one_fn)(void *);
typedef long (S *setlong_fn)(void *, long);
typedef long (S *getlong_fn)(void *, long *);
typedef long (S *setuint_fn)(void *, unsigned int);
typedef long (S *getuint_fn)(void *, unsigned int *);
typedef long (S *release_fn)(void *);

typedef struct { unsigned int size, flags, bytes, reserved; void *format; } Desc;
typedef struct { unsigned short tag, channels; unsigned int rate, avg; unsigned short align, bits, extra; } Wave;

int main(void) {
    void *ds=0, *primary=0, *b[40]={0}, *dup=0;
    Wave w={1,2,22050,88200,4,16,0};
    Desc d={sizeof(Desc),0,4096,0,&w}, p={sizeof(Desc),1,0,0,0};
    unsigned char *pcm1, *pcm2; unsigned int n1,n2,vfreq,status;
    void *p1,*p2; long value;
    int i;

    assert(DirectSoundCreate(0,&ds,0)==0 && ds);
    assert(CALL(coop_fn,ds,6)(ds,(void*)1,2)==0 && audio_open && opens==1);
    assert(CALL(bufcreate_fn,ds,3)(ds,&p,&primary,0)==0 && primary);
    assert(CALL(bufcreate_fn,ds,3)(ds,&d,&b[0],0)==0 && b[0]);
    assert(CALL(duplicate_fn,ds,5)(ds,b[0],&dup)==0 && dup && dup!=b[0]);

    assert(CALL(lock_fn,b[0],11)(b[0],4000,256,&p1,&n1,&p2,&n2,0)==0);
    assert(n1==96 && n2==160 && p2);
    memset(p1,0x5a,n1); memset(p2,0xa5,n2);
    assert(CALL(unlock_fn,b[0],19)(b[0],p1,n1,p2,n2)==0);

    assert(CALL(setlong_fn,b[0],15)(b[0],-2500)==0);
    assert(CALL(getlong_fn,b[0],6)(b[0],&value)==0 && value==-2500);
    assert(CALL(setlong_fn,b[0],16)(b[0],7000)==0);
    assert(CALL(getlong_fn,b[0],7)(b[0],&value)==0 && value==7000);
    assert(CALL(setuint_fn,b[0],17)(b[0],44100)==0);
    assert(CALL(getuint_fn,b[0],8)(b[0],&vfreq)==0 && vfreq==44100);
    assert(CALL(play_fn,b[0],12)(b[0],0,0,1)==0);
    assert(CALL(getuint_fn,b[0],9)(b[0],&status)==0 && (status&1));

    /* 40 live PCM buffers exceed the 32 host voices: Play must steal LRU, not fail/crash. */
    for (i=1;i<40;i++) assert(CALL(bufcreate_fn,ds,3)(ds,&d,&b[i],0)==0 && b[i]);
    for (i=1;i<40;i++) assert(CALL(play_fn,b[i],12)(b[i],0,0,0)==0);
    {
        com_dsound_stats st; com_dsound_get_stats(&st);
        assert(st.voice_steals>0 && st.voice_fail==0 && st.buffers_live==42);
    }
    assert(CALL(one_fn,b[39],18)(b[39])==0);
    assert(CALL(one_fn,b[0],18)(b[0])==0);
    assert(CALL(getlong_fn,b[0],6)(b[0],&value)==0 && value==-2500);
    assert(CALL(getlong_fn,b[0],7)(b[0],&value)==0 && value==7000);
    assert(CALL(getuint_fn,b[0],8)(b[0],&vfreq)==0 && vfreq==44100);
    assert(CALL(release_fn,dup,2)(dup)==0);
    for(i=0;i<40;i++) assert(CALL(release_fn,b[i],2)(b[i])==0);
    assert(CALL(release_fn,primary,2)(primary)==0);
    assert(CALL(release_fn,ds,2)(ds)==0);
    com_dsound_shutdown();
    assert(host_elf_audio_is_open()); /* Shutdown resets COM; it doesn't own the caller's audio device. */
    host_elf_audio_close();
    printf("x86_dsound_test: PASS (DirectSound, buffer APIs, 40 buffers, 32-voice LRU)\n");
    return 0;
}
