/* Isolated behavioral checks for win32_shim.c APIs owned by TASK-p-winproc.
 * Built i686 with section GC so unrelated runtime shims need not be linked. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/wait.h>
#include <sys/stat.h>

#define S __attribute__((stdcall))
typedef uint32_t u32;
extern u32 S GetCurrentThreadId(void);
extern u32 S FindWindowA(const char *, const char *);
extern u32 S FindWindowExA(u32, u32, const char *, const char *);
extern u32 S GetLastActivePopup(u32);
extern u32 S WaitForInputIdle(void *, u32);
extern int S EnumWindows(u32, long);
extern void *S CreateFileMappingA(void *, void *, u32, u32, u32, const char *);
extern void *S MapViewOfFile(void *, u32, u32, u32, u32);
extern int S UnmapViewOfFile(const void *);
extern int S CloseHandle(void *);
extern int S FlushFileBuffers(void *);
extern int S SetEndOfFile(void *);
extern u32 S GetTimeZoneInformation(void *);
extern int S IsDBCSLeadByte(uint8_t);
extern void S RaiseException(u32, u32, u32, const u32 *);
extern void S RtlUnwind(void *, void *, void *, void *);
extern void *win32_ui_lookup(const char *);
extern void reasm_elf_log(const char *);
extern void reasm_elf_log_hex(unsigned long);
uint32_t X86_ReadFsDword(uint32_t address) { return address==0 ? 0x1000u : 0; }
uint32_t X86_ReadMemProcedure(uint32_t address, uint32_t size) {
    if (size!=4) return 0;
    if (address==0x1000u) return 0xffffffffu;
    if (address==0x1004u) return 0x00401234u;
    return 0;
}
int arm_va_is_mapped(uint32_t address, uint32_t size) {
    return size==8 && address==0x1000u;
}

static u32 enum_proc, enum_args[2];
static int enum_status, enum_result, enum_calls;
int arm_guest_calln(u32 proc, unsigned nargs, const u32 *args, int *status) {
    enum_proc=proc; enum_calls++;
    if (nargs != 2) { *status=-99; return 0; }
    enum_args[0]=args[0]; enum_args[1]=args[1]; *status=enum_status;
    return enum_result;
}

static u32 S mock_active(void) { return 0x1234; }
static int S mock_class(u32 h, char *p, int n) {
    const char *s = h == 0x1234 ? "SWarClass" : ""; int i=0;
    while (s[i] && i<n-1) { p[i]=s[i]; ++i; } p[i]=0; return i;
}
static int S mock_title(u32 h, char *p, int n) {
    const char *s = h == 0x1234 ? "StarCraft" : ""; int i=0;
    while (s[i] && i<n-1) { p[i]=s[i]; ++i; } p[i]=0; return i;
}
void *win32_ui_lookup(const char *name) {
    if (!strcmp(name,"GetForegroundWindow") || !strcmp(name,"GetActiveWindow")) return mock_active;
    if (!strcmp(name,"GetClassNameA")) return mock_class;
    if (!strcmp(name,"GetWindowTextA")) return mock_title;
    return 0;
}
void reasm_elf_log(const char *s) { (void)write(2,s,strlen(s)); }
void reasm_elf_log_hex(unsigned long n) {
    static const char h[]="0123456789abcdef"; char b[10]="0x00000000"; int i;
    for(i=0;i<8;i++) b[2+i]=h[(n>>(28-4*i))&15];
    (void)write(2,b,sizeof b);
}
int win32_snp_vfile_own(void *h) { return h == (void *)(uintptr_t)0x534E5001u; }
static u32 worker_tid;
static void *thread_fn(void *p) { (void)p; worker_tid=GetCurrentThreadId(); return 0; }
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while(0)
int main(void) {
    pthread_t t; u32 main_tid=GetCurrentThreadId();
    char path[]="/tmp/winproc-map-XXXXXX"; int fd=mkstemp(path); void *fh, *m, *view;
    unsigned char tzi[172]; u32 tz;
    CHECK(main_tid && pthread_create(&t,0,thread_fn,0)==0);
    CHECK(pthread_join(t,0)==0 && worker_tid && worker_tid!=main_tid);
    CHECK(FindWindowA("SWarClass",0)==0x1234);
    CHECK(FindWindowA("swARclass","starcraft")==0x1234);
    CHECK(FindWindowA("wrong",0)==0);
    CHECK(FindWindowExA(0,0,"SWarClass",0)==0x1234);
    CHECK(FindWindowExA(0x1234,0,"SWarClass",0)==0);
    CHECK(GetLastActivePopup(0x1234)==0x1234);
    CHECK(WaitForInputIdle(0,0)==0);
    CHECK(EnumWindows(0,0)==0 && enum_calls==0); /* reject null callback */
    enum_result=1; enum_status=0;
    CHECK(EnumWindows(0x12345678,0x5678)==1);
    CHECK(enum_calls==1 && enum_proc==0x12345678 && enum_args[0]==0x1234 && enum_args[1]==0x5678);
    enum_result=0;
    CHECK(EnumWindows(0x12345678,0)==0 && enum_calls==2); /* guest callback requests stop */
    enum_status=-4;
    CHECK(EnumWindows(0x12345678,0)==0 && enum_calls==3); /* invalid guest proc fails closed */
    CHECK(write(fd,"mapped-data",11)==11);
    fh=(void *)(uintptr_t)(fd+1);
    CHECK(CreateFileMappingA((void *)(uintptr_t)0x534E5001u,0,2,0,0,0)==0); /* virtual standard.snp */
    m=CreateFileMappingA(fh,0,2,0,0,0); /* PAGE_READONLY */
    CHECK(m!=0);
    view=MapViewOfFile(m,4,0,0,0); /* FILE_MAP_READ */
    CHECK(view && !memcmp(view,"mapped-data",11));
    CHECK(UnmapViewOfFile(view)==1);
    CHECK(UnmapViewOfFile(view)==0);
    CHECK(CloseHandle(m)==1 && MapViewOfFile(m,4,0,0,1)==0);
    CHECK(FlushFileBuffers(fh)==1);
    CHECK(lseek(fd,5,SEEK_SET)==5 && SetEndOfFile(fh)==1);
    CHECK(fstat(fd,(struct stat[1]){{0}})==0);
    close(fd); unlink(path);
    memset(tzi,0xFF,sizeof tzi); tz=GetTimeZoneInformation(tzi);
    CHECK(tz==0 || tz==1 || tz==2);
    CHECK(((uint16_t *)(tzi+4))[0]!=0);
    CHECK(((uint16_t *)(tzi+4))[31]==0 && ((uint16_t *)(tzi+84))[31]==0);
    CHECK(!IsDBCSLeadByte(0x81) && !IsDBCSLeadByte(0xE0)); /* active ACP is 1252 */
    CHECK(!IsDBCSLeadByte('A') && !IsDBCSLeadByte(0xA0));
    { pid_t p; int st, fds[2]; char out[2048]; ssize_t n;
      CHECK(pipe(fds)==0); p=fork(); CHECK(p>=0);
      if (!p) { close(fds[0]); dup2(fds[1],2); close(fds[1]); RaiseException(0xE1234567,1,2,(u32[]){0x11,0x22}); }
      close(fds[1]); { size_t used=0; while (used<sizeof(out)-1 && (n=read(fds[0],out+used,sizeof(out)-1-used))>0) used+=(size_t)n; out[used]=0; } close(fds[0]); CHECK(out[0]!=0);
      CHECK(waitpid(p,&st,0)==p && WIFEXITED(st) && WEXITSTATUS(st)==70);
      CHECK(strstr(out,"fs:[0]=0x00001000") && strstr(out,"handler=0x00401234") && strstr(out,"<end>")); }
    { pid_t p; int st, fds[2]; char out[2048]; ssize_t n;
      CHECK(pipe(fds)==0); p=fork(); CHECK(p>=0);
      if (!p) { close(fds[0]); dup2(fds[1],2); close(fds[1]); RtlUnwind((void*)1,(void*)2,(void*)3,(void*)4); }
      close(fds[1]); { size_t used=0; while (used<sizeof(out)-1 && (n=read(fds[0],out+used,sizeof(out)-1-used))>0) used+=(size_t)n; out[used]=0; } close(fds[0]); CHECK(out[0]!=0);
      CHECK(waitpid(p,&st,0)==p && WIFEXITED(st) && WEXITSTATUS(st)==70);
      CHECK(strstr(out,"fs:[0]=0x00001000") && strstr(out,"handler=0x00401234") && strstr(out,"<end>")); }
    puts("x86_win32_shim_test: PASS"); return 0;
}
