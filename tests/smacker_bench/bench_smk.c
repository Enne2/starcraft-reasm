#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef void *smk_t;
typedef smk_t (*open_memory_fn)(const unsigned char *, unsigned long);
typedef void (*close_fn)(smk_t);
typedef char (*info_all_fn)(const smk_t, unsigned long *, unsigned long *, double *);
typedef char (*info_video_fn)(const smk_t, unsigned long *, unsigned long *, unsigned char *);
typedef char (*info_audio_fn)(const smk_t, unsigned char *, unsigned char[7], unsigned char[7], unsigned long[7]);
typedef char (*enable_video_fn)(smk_t, unsigned char);
typedef char (*enable_audio_fn)(smk_t, unsigned char, unsigned char);
typedef char (*decode_fn)(smk_t);
typedef const unsigned char *(*get_data_fn)(const smk_t);
typedef const unsigned char *(*get_audio_fn)(const smk_t, unsigned char);
typedef unsigned long (*get_audio_size_fn)(const smk_t, unsigned char);

typedef struct {
    void *handle;
    open_memory_fn open_memory;
    close_fn close;
    info_all_fn info_all;
    info_video_fn info_video;
    info_audio_fn info_audio;
    enable_video_fn enable_video;
    enable_audio_fn enable_audio;
    decode_fn first, next;
    get_data_fn palette, video;
    get_audio_fn audio;
    get_audio_size_fn audio_size;
} api_t;

static int symbol(void *h, const char *name, void *dst, size_t n) {
    void *p = dlsym(h, name);
    if (!p) { fprintf(stderr, "missing symbol %s: %s\n", name, dlerror()); return 0; }
    if (n != sizeof p) { fprintf(stderr, "function pointer size mismatch\n"); return 0; }
    memcpy(dst, &p, n); return 1;
}
#define LOAD(a, field, name) symbol((a)->handle, name, &(a)->field, sizeof((a)->field))
static int load_api(api_t *a, const char *path) {
    memset(a, 0, sizeof *a);
    a->handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!a->handle) { fprintf(stderr, "dlopen %s: %s\n", path, dlerror()); return 0; }
    return LOAD(a,open_memory,"smk_open_memory") && LOAD(a,close,"smk_close") &&
      LOAD(a,info_all,"smk_info_all") && LOAD(a,info_video,"smk_info_video") &&
      LOAD(a,info_audio,"smk_info_audio") && LOAD(a,enable_video,"smk_enable_video") &&
      LOAD(a,enable_audio,"smk_enable_audio") && LOAD(a,first,"smk_first") &&
      LOAD(a,next,"smk_next") && LOAD(a,palette,"smk_get_palette") &&
      LOAD(a,video,"smk_get_video") && LOAD(a,audio,"smk_get_audio") &&
      LOAD(a,audio_size,"smk_get_audio_size");
}
static unsigned char *read_file(const char *path, unsigned long *size) {
    FILE *f = fopen(path,"rb"); long n; unsigned char *p;
    if (!f || fseek(f,0,SEEK_END) || (n=ftell(f))<0 || fseek(f,0,SEEK_SET)) { perror(path); if(f)fclose(f); return NULL; }
    p=(unsigned char*)malloc((size_t)n); if(!p){perror("malloc");fclose(f);return NULL;}
    if(fread(p,1,(size_t)n,f)!=(size_t)n){perror("fread");free(p);fclose(f);return NULL;}
    fclose(f); *size=(unsigned long)n; return p;
}
static double now_sec(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return (double)t.tv_sec+(double)t.tv_nsec*1e-9; }
static int write_all(FILE *f,const void *p,size_t n){return fwrite(p,1,n,f)==n;}
static int decode_all(api_t *a, const unsigned char *data, unsigned long bytes, FILE *dump,
                      double *open_s, double *decode_s) {
    smk_t s; unsigned long frame=0, frames=0,w=0,h=0; unsigned char y=0,mask=0,ch[7],bd[7]; unsigned long rate[7]; double usf;
    double t0=now_sec(); s=a->open_memory(data,bytes); *open_s=now_sec()-t0;
    if(!s){fprintf(stderr,"smk_open_memory failed\n");return 0;}
    if(a->info_all(s,&frame,&frames,&usf)||a->info_video(s,&w,&h,&y)||a->info_audio(s,&mask,ch,bd,rate)){fprintf(stderr,"info failed\n");a->close(s);return 0;}
    if(w>65535||h>65535||w*h>0x40000000ul){fprintf(stderr,"implausible dimensions %lu x %lu\n",w,h);a->close(s);return 0;}
    a->enable_video(s,1); for(unsigned i=0;i<7;i++) if(mask&(1u<<i)) a->enable_audio(s,(unsigned char)i,1);
    if(dump){ uint32_t hdr[5]={0x534d4b44u,1,(uint32_t)w,(uint32_t)h,(uint32_t)frames}; if(!write_all(dump,hdr,sizeof hdr)){perror("dump");a->close(s);return 0;} }
    t0=now_sec();
    if(a->first(s)==(char)-1){fprintf(stderr,"smk_first failed\n");a->close(s);return 0;}
    for(unsigned long i=0;i<frames;i++) {
        uint32_t rec[2]={(uint32_t)i,(uint32_t)(w*h)}; const unsigned char *v=a->video(s),*p=a->palette(s);
        if(dump){
            if(!v||!p||!write_all(dump,rec,sizeof rec)||!write_all(dump,v,(size_t)(w*h))||!write_all(dump,p,768)){fprintf(stderr,"video/palette dump failed at frame %lu\n",i);a->close(s);return 0;}
            for(unsigned tr=0;tr<7;tr++) { uint32_t n=(uint32_t)a->audio_size(s,(unsigned char)tr); const unsigned char *au=a->audio(s,(unsigned char)tr);
                if(!write_all(dump,&n,sizeof n)||(n&&(!au||!write_all(dump,au,n)))){fprintf(stderr,"audio dump failed\n");a->close(s);return 0;}
            }
        }
        if(i+1<frames && a->next(s)==(char)-1){fprintf(stderr,"smk_next failed at frame %lu\n",i+1);a->close(s);return 0;}
    }
    *decode_s=now_sec()-t0; a->close(s); return 1;
}
static int cmp_double(const void *a,const void *b){double x=*(const double*)a,y=*(const double*)b;return (x>y)-(x<y);}
static void stats(const char *label,double *v,unsigned n){qsort(v,n,sizeof *v,cmp_double);printf("%s median=%.6f s min=%.6f s max=%.6f s span=%.6f s (%u runs)\n",label,v[n/2],v[0],v[n-1],v[n-1]-v[0],n);}
int main(int argc,char **argv){
    if(argc<4){fprintf(stderr,"Usage: %s <library.so> --dump <file> <sample.smk> | --bench <runs> <sample.smk> [sample.smk...]\n",argv[0]);return 2;}
    api_t a; if(!load_api(&a,argv[1]))return 2;
    int dump=!strcmp(argv[2],"--dump"), bench=!strcmp(argv[2],"--bench"); if(!dump&&!bench){fprintf(stderr,"expected --dump or --bench\n");return 2;}
    if(dump&&argc!=5){fprintf(stderr,"--dump requires output and one sample\n");return 2;}
    int runs=1, first_sample=4; if(bench){runs=atoi(argv[3]); if(runs<3||runs>100){fprintf(stderr,"runs must be >=3 and <=100\n");return 2;} if(argc<5){fprintf(stderr,"--bench requires samples\n");return 2;}}
    FILE *out=dump?fopen(argv[3],"wb"):NULL; if(dump&&!out){perror(argv[3]);return 2;}
    for(int si=first_sample;si<argc;si++){
        unsigned long bytes=0; unsigned char *data=read_file(argv[si],&bytes); if(!data)return 2;
        printf("sample=%s bytes=%lu\n",argv[si],bytes);
        if(dump){double o,d; int ok=decode_all(&a,data,bytes,out,&o,&d); if(!ok){free(data);fclose(out);return 1;} }
        else {double *opens=calloc((size_t)runs,sizeof *opens),*decs=calloc((size_t)runs,sizeof *decs);if(!opens||!decs)return 2;
          for(int r=0;r<runs;r++){if(!decode_all(&a,data,bytes,NULL,&opens[r],&decs[r]))return 1;}
          stats("open/tree-build",opens,(unsigned)runs);stats("decode(first+next)",decs,(unsigned)runs);free(opens);free(decs);}
        free(data);
    }
    if(out&&fclose(out)){perror("fclose");return 1;} dlclose(a.handle); return 0;
}
