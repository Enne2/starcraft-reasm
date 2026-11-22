#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include "../runtime/shim-port/elf/win32_shim.c"
struct timespec { long tv_sec; long tv_nsec; };
extern int clock_gettime(int,void *);
extern int getpid(void);
extern int pipe(int[2]);
void reasm_elf_log(const char *s) { (void)s; }
void reasm_elf_log_hex(unsigned int v) { (void)v; }

#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x);return 1;} } while(0)
#define FILE_SIZE (512u*1024u)
#define SAMPLE_COUNT 576u
static unsigned char fixture[FILE_SIZE];
static char path_native[128];
static ui_mutex_t op_mutex=PTHREAD_MUTEX_INITIALIZER;
static void *shared_handle;
static double now_s(void) { struct timespec t;clock_gettime(1,&t);return t.tv_sec+t.tv_nsec/1e9; }
static int scan_cached(void *h) {
    unsigned i;unsigned char got[32];u32 n;
    for(i=0;i<SAMPLE_COUNT;i++) {
        u32 off=i*0x200u;
        if(SetFilePointer(h,(long)off,0,0)!=(long)off)return 0;
        if(!ReadFile(h,got,sizeof got,&n,0)||n!=sizeof got||memcmp(got,fixture+off,sizeof got))return 0;
    }
    return 1;
}
static int scan_direct(int fd) {
    unsigned i;unsigned char got[32];
    for(i=0;i<SAMPLE_COUNT;i++) {
        u32 off=i*0x200u;
        if(lseek(fd,(long)off,0)!=(long)off)return 0;
        if(read(fd,got,sizeof got)!=(long)sizeof got||memcmp(got,fixture+off,sizeof got))return 0;
    }
    return 1;
}
static void *thread_scan(void *arg) {
    unsigned start=(unsigned)(ulong)arg*256u,i;unsigned char b[32];u32 n;
    for(i=0;i<256;i++) {
        u32 off=(start+i)*0x200u;
        pthread_mutex_lock(&op_mutex);
        if(SetFilePointer(shared_handle,(long)off,0,0)!=(long)off||!ReadFile(shared_handle,b,sizeof b,&n,0)||n!=sizeof b||memcmp(b,fixture+off,sizeof b)) {
            pthread_mutex_unlock(&op_mutex);return (void*)1;
        }
        pthread_mutex_unlock(&op_mutex);
    }
    return 0;
}
int main(void) {
    unsigned i,j;int fd,pfd[2];void *h;u32 n;unsigned char b[128u*1024u];unsigned long tid[2];void *tr[2];
    double cache_runs[5],direct_runs[5],a,z,tmp;u32 before,cold_calls;
    for(i=0;i<FILE_SIZE;i++)fixture[i]=(unsigned char)((i*37u+(i>>7))&255u);
    { int p=getpid();snprintf(path_native,sizeof path_native,"/tmp/pi-io-readahead-%d.bin",p); }
    fd=open(path_native,0101|01000,0600);CHECK(fd>=0);CHECK(write(fd,fixture,FILE_SIZE)==FILE_SIZE);close(fd);
    fd=open(path_native,0);CHECK(fd>=0);h=FD2H(fd);file_cache_register(h,1);
    CHECK((long)SetFilePointer(h,0,0,0)==0);
    CHECK(pipe(pfd)==0);file_cache_register(FD2H(pfd[0]),1);
    CHECK(file_cache_lock_handle(FD2H(pfd[0]))==0);close(pfd[0]);close(pfd[1]);
    file_cache_register(FD2H(1),1);CHECK(file_cache_lock_handle(FD2H(1))==0);
    before=g_file_cache_pread_calls;CHECK(scan_cached(h));cold_calls=g_file_cache_pread_calls-before;CHECK(cold_calls>0&&cold_calls<=5);

    /* Random offsets and a large direct-read bypass must match the source bytes. */
    for(i=0;i<1500;i++) {
        u32 off=(i*7919u)%(FILE_SIZE-64u);unsigned char q[64];
        CHECK((u32)SetFilePointer(h,(long)off,0,0)==off);CHECK(ReadFile(h,q,sizeof q,&n,0)&&n==sizeof q);
        CHECK(memcmp(q,fixture+off,sizeof q)==0);
    }
    CHECK((u32)SetFilePointer(h,0,0,0)==0);CHECK(ReadFile(h,b,sizeof b,&n,0)&&n==sizeof b);CHECK(!memcmp(b,fixture,sizeof b));
    CHECK((u32)SetFilePointer(h,FILE_SIZE-10,0,0)==FILE_SIZE-10);CHECK(ReadFile(h,b,32,&n,0)&&n==10);CHECK(!memcmp(b,fixture+FILE_SIZE-10,10));
    CHECK((u32)SetFilePointer(h,-10,0,1)==FILE_SIZE-10);
    CHECK((u32)SetFilePointer(h,-32,0,2)==FILE_SIZE-32);
    CHECK(ReadFile(h,b,32,&n,0)&&n==32&&!memcmp(b,fixture+FILE_SIZE-32,32));

    shared_handle=h;
    CHECK(pthread_create(&tid[0],0,thread_scan,(void*)0)==0);CHECK(pthread_create(&tid[1],0,thread_scan,(void*)1)==0);
    CHECK(pthread_join(tid[0],&tr[0])==0&&tr[0]==0);CHECK(pthread_join(tid[1],&tr[1])==0&&tr[1]==0);

    /* 5-run scan benchmark, cache vs the old seek+read syscall pattern. */
    for(j=0;j<5;j++){a=now_s();CHECK(scan_cached(h));z=now_s();cache_runs[j]=z-a;}
    fd=H2FD(h);
    for(j=0;j<5;j++){a=now_s();CHECK(scan_direct(fd));z=now_s();direct_runs[j]=z-a;}
    for(i=0;i<5;i++)for(j=i+1;j<5;j++) {
        if(cache_runs[j]<cache_runs[i]){tmp=cache_runs[i];cache_runs[i]=cache_runs[j];cache_runs[j]=tmp;}
        if(direct_runs[j]<direct_runs[i]){tmp=direct_runs[i];direct_runs[i]=direct_runs[j];direct_runs[j]=tmp;}
    }
    printf("cached_scan_s min=%.6f median=%.6f max=%.6f span=%.6f\n",cache_runs[0],cache_runs[2],cache_runs[4],cache_runs[4]-cache_runs[0]);
    printf("direct_scan_s min=%.6f median=%.6f max=%.6f span=%.6f\n",direct_runs[0],direct_runs[2],direct_runs[4],direct_runs[4]-direct_runs[0]);
    printf("cold_scan_pread64_calls=%u; direct seek+read calls per scan=%u\n",cold_calls,SAMPLE_COUNT*2u);
    printf("cached_pread64_calls_total=%u\n",g_file_cache_pread_calls);
    CHECK(file_cache_close(h));unlink(path_native);
    puts("x86_io_readahead_test: PASS");return 0;
}
