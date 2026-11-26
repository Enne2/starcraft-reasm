#define _POSIX_C_SOURCE 200809L
#include "sc_ctl.h"
#include "host_elf.h"
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define SC_CTL_W 640
#define SC_CTL_HEIGHT 480
#define SC_CTL_PIXELS (SC_CTL_W * SC_CTL_HEIGHT)
#define SC_CTL_RGB (SC_CTL_PIXELS * 3)
#define SC_CTL_LINE 2048

static unsigned char snap[SC_CTL_RGB];
static unsigned char snap_idx[SC_CTL_PIXELS];
static atomic_int snap_state; /* 0 idle, 1 RGB requested, 2 ready, 3 indexed requested */
static atomic_ulong presented_count;
static atomic_int palette_present;
static atomic_int running;
static atomic_int enabled;
static atomic_int text_state; /* 0 idle, 1 pending, 2 consumed */
static char text_buf[32];
static pthread_t server_thread;
static int unix_fd = -1, tcp_fd = -1;
static char unix_path[sizeof(((struct sockaddr_un *)0)->sun_path)];

static unsigned int crc32_bytes(const unsigned char *p, size_t n) {
    unsigned int c = 0xffffffffu; size_t i; int b;
    for (i=0;i<n;++i) { c ^= p[i]; for (b=0;b<8;++b) c=(c>>1)^(0xedb88320u & (0u-(c&1u))); }
    return c ^ 0xffffffffu;
}
static void be32(unsigned char *p, unsigned int v) { p[0]=v>>24; p[1]=v>>16; p[2]=v>>8; p[3]=v; }
static int write_all(int fd, const void *buf, size_t n) {
    const unsigned char *p=(const unsigned char *)buf;
    while(n) { ssize_t w=write(fd,p,n); if(w<0 && errno==EINTR) continue; if(w<=0) return -1; p+=w; n-=(size_t)w; }
    return 0;
}
static int png_chunk(int fd, const char type[4], const unsigned char *data, unsigned int len) {
    unsigned char h[8], c[4]; unsigned int crc;
    be32(h,len); memcpy(h+4,type,4);
    crc=crc32_bytes((const unsigned char *)type,4);
    if(len) {
        /* CRC over type+data without requiring a temporary allocation. */
        unsigned int c0=0xffffffffu; size_t i; int b;
        for(i=0;i<4u+len;++i) { unsigned char v=i<4?((const unsigned char *)type)[i]:data[i-4]; c0^=v; for(b=0;b<8;++b)c0=(c0>>1)^(0xedb88320u&(0u-(c0&1u))); }
        crc=c0^0xffffffffu;
    }
    be32(c,crc);
    return write_all(fd,h,8) || (len && write_all(fd,data,len)) || write_all(fd,c,4) ? -1:0;
}
static int save_raw(const char *path, const unsigned char *pixels) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;
    if (write_all(fd, pixels, SC_CTL_PIXELS) != 0) { close(fd); return -1; }
    close(fd);
    return 0;
}
static int save_png(const char *path, const unsigned char *rgb) {
    unsigned char ihdr[13], zhead[2]={0x78,0x01}, block[5], adbuf[4];
    unsigned int adler_a=1,adler_b=0;
    unsigned char *raw; size_t raw_n=(SC_CTL_W*3u+1u)*SC_CTL_HEIGHT, at=0, i; int fd,y,rc=-1;
    static const unsigned char sig[8]={137,80,78,71,13,10,26,10};
    raw=(unsigned char *)malloc(raw_n); if(!raw) return -1;
    for(y=0;y<SC_CTL_HEIGHT;++y) { raw[at++]=0; memcpy(raw+at,rgb+(size_t)y*SC_CTL_W*3,SC_CTL_W*3); at+=SC_CTL_W*3; }
    fd=open(path,O_WRONLY|O_CREAT|O_TRUNC,0644); if(fd<0) { free(raw); return -1; }
    be32(ihdr,SC_CTL_W); be32(ihdr+4,SC_CTL_HEIGHT); ihdr[8]=8; ihdr[9]=2; ihdr[10]=ihdr[11]=ihdr[12]=0;
    if(write_all(fd,sig,8)||png_chunk(fd,"IHDR",ihdr,13)) goto done;
    if(png_chunk(fd,"IDAT",zhead,2)) goto done;
    for(i=0;i<raw_n;++i) { adler_a=(adler_a+raw[i])%65521u; adler_b=(adler_b+adler_a)%65521u; }
    for(at=0;at<raw_n;) {
        unsigned int n=(unsigned int)(raw_n-at); if(n>65535u)n=65535u;
        block[0]=(at+n==raw_n)?1:0; block[1]=(unsigned char)n; block[2]=(unsigned char)(n>>8);
        block[3]=(unsigned char)~n; block[4]=(unsigned char)(~n>>8);
        /* IDAT is split per stored block; each chunk has its own CRC. */
        {
            size_t payload_n=5u+n; unsigned char *payload=(unsigned char *)malloc(payload_n);
            if(!payload) goto done;
            memcpy(payload,block,5); memcpy(payload+5,raw+at,n);
            if(png_chunk(fd,"IDAT",payload,(unsigned int)payload_n)) { free(payload); goto done; }
            free(payload);
        }
        at+=n;
    }
    be32(adbuf,(adler_b<<16)|adler_a);
    if(png_chunk(fd,"IDAT",adbuf,4)||png_chunk(fd,"IEND",0,0)) goto done;
    rc=0;
done:
    close(fd); free(raw); return rc;
}
static int send_line(int fd,const char *s) {
    size_t n=strlen(s); const char *p=s;
    while(n){ssize_t w=send(fd,p,n,MSG_NOSIGNAL);if(w<0&&errno==EINTR)continue;if(w<=0)return -1;p+=w;n-=(size_t)w;}
    return 0;
}
static int parse_key(const char *s) {
    static const struct { const char *n; int k; } keys[]={
        {"esc",27},{"enter",13},{"space",32},{"tab",9},{"backspace",8},
        {"left",0x40000050},{"right",0x4000004f},{"up",0x40000052},{"down",0x40000051},
        {"f1",0x4000003a},{"f2",0x4000003b},{"f3",0x4000003c},{"f4",0x4000003d},
        {"f5",0x4000003e},{"f6",0x4000003f},{"f7",0x40000040},{"f8",0x40000041},
        {"f9",0x40000042},{"f10",0x40000043},{"f11",0x40000044},{"f12",0x40000045},
        {0,0}}; int i; char *end; long v;
    for(i=0;keys[i].n;++i) if(!strcmp(s,keys[i].n)) return keys[i].k;
    if(s[0] && !s[1]) return (unsigned char)s[0];
    v=strtol(s,&end,0); return *s && !*end && v>=0 && v<=0x7fffffffL?(int)v:-1;
}
static void post(int type,int x,int y,int key) { struct host_input_event e; memset(&e,0,sizeof e); e.type=type;e.x=x;e.y=y;e.key=key;host_elf_test_event(&e); }
static void handle(int fd,char *line) {
    char out[SC_CTL_LINE],*arg,*end; int x,y,btn=1,k; unsigned long bytes; struct timespec ts;
    while(*line==' '||*line=='\t') ++line;
    end=line+strlen(line);
    while(end>line&&(end[-1]=='\r'||end[-1]==' '||end[-1]=='\t')) *--end=0;
    if(!*line)return;
    if(!strcmp(line,"quit")){send_line(fd,"ok quit\n");shutdown(fd,SHUT_RDWR);return;}
    if(!strcmp(line,"hash")) {
        extern unsigned char *host_elf_canvas(int *, int *, int *);
        extern unsigned long host_elf_presented_at_ms(void);
        int cw=0,ch=0,cp=0; unsigned char *cv=host_elf_canvas(&cw,&ch,&cp);
        unsigned long h=2166136261ul, i, n = cv ? (unsigned long)cp*(unsigned long)ch : 0;
        for(i=0;i<n;i+=3u) { h=(h ^ cv[i])*16777619ul; }
        snprintf(out,sizeof out,"ok hash f=%lu ms=%lu presented=%lu\n",
                 (h>>8)&0xFFFFFFFFul, host_elf_presented_at_ms(), atomic_load(&presented_count));
        send_line(fd,out); return;
    }
    if(!strcmp(line,"state")) { snprintf(out,sizeof out,"ok state frame=%lu w=640 h=480 presented=%lu pal=%d\n",atomic_load(&presented_count),atomic_load(&presented_count),atomic_load(&palette_present));send_line(fd,out);return; }
    if(!strncmp(line,"shot ",5) || !strncmp(line,"rawshot ",8)) {
        int raw = !strncmp(line,"rawshot ",8);
        int request = raw ? 3 : 1;
        arg=line+(raw?8:5); if(!*arg){send_line(fd,"err shot path\n");return;}
        { int expected=0; if(!atomic_compare_exchange_strong(&snap_state,&expected,request)){send_line(fd,"err shot busy\n");return;} }
        clock_gettime(CLOCK_MONOTONIC,&ts);
        for(;;) { struct timespec now,nap={0,1000000}; if(atomic_load_explicit(&snap_state,memory_order_acquire)==2)break; nanosleep(&nap,0); clock_gettime(CLOCK_MONOTONIC,&now); if(!atomic_load(&running)||now.tv_sec-ts.tv_sec>10){atomic_store(&snap_state,0);send_line(fd,"err shot timeout\n");return;} }
        if((raw?save_raw(arg,snap_idx):save_png(arg,snap))){atomic_store(&snap_state,0);snprintf(out,sizeof out,"err shot %s\n",strerror(errno));send_line(fd,out);return;}
        if(raw) {
            char rgb_path[SC_CTL_LINE]; size_t n=strlen(arg);
            if(n+5>=sizeof rgb_path){atomic_store(&snap_state,0);send_line(fd,"err rawshot path too long\n");return;}
            memcpy(rgb_path,arg,n); memcpy(rgb_path+n,".png",5);
            if(save_png(rgb_path,snap)){atomic_store(&snap_state,0);snprintf(out,sizeof out,"err rawshot RGB %s\n",strerror(errno));send_line(fd,out);return;}
        }
        { int f=open(arg,O_RDONLY); off_t sz=f>=0?lseek(f,0,SEEK_END):-1; if(f>=0)close(f); bytes=sz>0?(unsigned long)sz:0; }
        atomic_store_explicit(&snap_state,0,memory_order_release);
        snprintf(out,sizeof out,"ok %s %s %lu\n",raw?"rawshot":"shot",arg,bytes);send_line(fd,out);return;
    }
    if(sscanf(line,"move %d %d",&x,&y)==2){post(HOST_INPUT_MOVE,x,y,0);send_line(fd,"ok move\n");return;}
    if(sscanf(line,"click %d %d %d",&x,&y,&btn)>=2){if(btn!=1&&btn!=2){send_line(fd,"err btn\n");return;}post(HOST_INPUT_MOVE,x,y,0);post(HOST_INPUT_DOWN,x,y,btn);post(HOST_INPUT_UP,x,y,btn);send_line(fd,"ok click\n");return;}
    if(sscanf(line,"down %d %d %d",&x,&y,&btn)>=2){post(HOST_INPUT_MOVE,x,y,0);post(HOST_INPUT_DOWN,x,y,btn);send_line(fd,"ok down\n");return;}
    if(sscanf(line,"up %d %d %d",&x,&y,&btn)>=2){post(HOST_INPUT_MOVE,x,y,0);post(HOST_INPUT_UP,x,y,btn);send_line(fd,"ok up\n");return;}
    if(!strncmp(line,"key ",4)||!strncmp(line,"keydown ",8)||!strncmp(line,"keyup ",6)){
        int type=line[0]=='k'&&line[1]=='e'?HOST_INPUT_KEYDOWN:HOST_INPUT_KEYDOWN;
        if(!strncmp(line,"keydown ",8))type=HOST_INPUT_KEYDOWN; else if(!strncmp(line,"keyup ",6))type=HOST_INPUT_KEYUP;
        arg=line+(type==HOST_INPUT_KEYDOWN&& !strncmp(line,"keydown ",8)?8:(!strncmp(line,"keyup ",6)?6:4)); k=parse_key(arg);
        if(k<0){send_line(fd,"err key\n");return;}
        post(type,0,0,k); if(!strncmp(line,"key ",4)){struct timespec wait={0,50000000};nanosleep(&wait,0);post(HOST_INPUT_KEYUP,0,0,k);} send_line(fd,"ok key\n");return;
    }
    if(!strncmp(line,"text ",5)){
        size_t n=strlen(line+5); int expected=0;
        if(n>31){send_line(fd,"err text too long (max 31 bytes)\n");return;}
        if(!atomic_compare_exchange_strong(&text_state,&expected,3)){send_line(fd,"err text busy\n");return;}
        memcpy(text_buf,line+5,n+1);
        atomic_store_explicit(&text_state,1,memory_order_release);
        clock_gettime(CLOCK_MONOTONIC,&ts);
        for(;;){int v=atomic_load_explicit(&text_state,memory_order_acquire);struct timespec now,wait={0,1000000};if(v==2)break;nanosleep(&wait,0);clock_gettime(CLOCK_MONOTONIC,&now);if(!atomic_load(&running)||now.tv_sec-ts.tv_sec>10){atomic_store(&text_state,0);send_line(fd,"err text timeout\n");return;}}
        atomic_store(&text_state,0);send_line(fd,"ok text\n");return;
    }
    send_line(fd,"err command\n");
}
static void serve_client(int fd) {
    char line[SC_CTL_LINE]; size_t used=0; char c;
    while(atomic_load(&running)) { ssize_t n=read(fd,&c,1); if(n<0&&errno==EINTR)continue; if(n<=0)break; if(c=='\n'){line[used]=0;handle(fd,line);used=0;continue;} if(used+1<sizeof line)line[used++]=c;else {send_line(fd,"err line too long\n");used=0;} }
    close(fd);
}
static void *server_main(void *unused) {
    (void)unused;
    while(atomic_load(&running)) {
        struct pollfd p[2]; nfds_t n=0,i; int r;
        if(unix_fd>=0){p[n].fd=unix_fd;p[n].events=1;p[n++].revents=0;} if(tcp_fd>=0){p[n].fd=tcp_fd;p[n].events=1;p[n++].revents=0;}
        if(!n)break;
        r=poll(p,n,500); if(r<=0)continue;
        for(i=0;i<n;++i)if(p[i].revents&1){int fd=accept(p[i].fd,0,0);if(fd>=0)serve_client(fd);}
    }
    return 0;
}
static int make_unix(const char *path) {
    struct sockaddr_un a; int fd;
    if(strlen(path)>=sizeof a.sun_path)return -1;
    fd=socket(AF_UNIX,SOCK_STREAM,0);if(fd<0)return -1;memset(&a,0,sizeof a);a.sun_family=AF_UNIX;strcpy(a.sun_path,path);
    /* un socket orfano (kill -9 dell'istanza precedente) farebbe fallire il bind: se nessuno
     * risponde, il file e' nostro e si puo' rimuovere. */
    if(bind(fd,(struct sockaddr *)&a,sizeof a)!=0 && *path){ unlink(path);
        if(bind(fd,(struct sockaddr *)&a,sizeof a)!=0){close(fd);return -1;} }
    if(listen(fd,4)){close(fd);return -1;}strcpy(unix_path,path);return fd;
}
static int make_tcp(const char *port) {
    struct sockaddr_in a; char *end; long p=strtol(port,&end,10);int fd,one=1;
    if(*port==0||*end||p<1||p>65535)return -1;
    fd=socket(AF_INET,SOCK_STREAM,0);if(fd<0)return -1;
    setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&one,sizeof one);memset(&a,0,sizeof a);a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(0x7f000001u);a.sin_port=htons((unsigned short)p);
    if(bind(fd,(struct sockaddr *)&a,sizeof a)||listen(fd,4)){close(fd);return -1;}return fd;
}
int sc_ctl_start(void) {
    const char *enabled_env=getenv("SC_CTL"),*path=getenv("SC_CTL_SOCK"),*port=getenv("SC_CTL_PORT");
    if(enabled_env&&!strcmp(enabled_env,"0"))return 0;
    if(!path||!*path)path="./sc-ctl.sock";
    unix_fd=make_unix(path); if(unix_fd<0)fprintf(stderr,"[sc_ctl] socket Unix non disponibile (%s): %s\n",path,strerror(errno));
    if(port&&*port){tcp_fd=make_tcp(port);if(tcp_fd<0)fprintf(stderr,"[sc_ctl] TCP 127.0.0.1:%s non disponibile: %s\n",port,strerror(errno));}
    if(unix_fd<0&&tcp_fd<0)return -1;
    atomic_store(&running,1);
    if(pthread_create(&server_thread,0,server_main,0)){atomic_store(&running,0);if(unix_fd>=0){close(unix_fd);unix_fd=-1;}if(tcp_fd>=0){close(tcp_fd);tcp_fd=-1;}if(unix_path[0]){unlink(unix_path);unix_path[0]=0;}return -1;}
    pthread_detach(server_thread);atomic_store(&enabled,1);
    fprintf(stderr,"[sc_ctl] attivo socket=%s%s%s\n",unix_fd>=0?path:"(none)",port&&*port?" tcp=127.0.0.1:":"",port&&*port?port:"");return 0;
}
void sc_ctl_present(const unsigned char *canvas,int w,int h,int pitch,const unsigned int pal[256]) {
    int state,x,y;
    if(!atomic_load_explicit(&enabled,memory_order_relaxed))return;
    state=atomic_load_explicit(&snap_state,memory_order_acquire);
    if((state==1||state==3)&&canvas&&pal&&w>=SC_CTL_W&&h>=SC_CTL_HEIGHT&&pitch>=SC_CTL_W){
        for(y=0;y<SC_CTL_HEIGHT;++y)for(x=0;x<SC_CTL_W;++x){
            unsigned char idx=canvas[(size_t)y*pitch+x];
            size_t o=((size_t)y*SC_CTL_W+x)*3;
            if(state==3) snap_idx[(size_t)y*SC_CTL_W+x]=idx;
            {unsigned int c=pal[idx];snap[o]=c>>16;snap[o+1]=c>>8;snap[o+2]=c;}
        }
        atomic_store_explicit(&snap_state,2,memory_order_release);
    }
    if(atomic_load(&text_state)==1){struct host_input_event e;memset(&e,0,sizeof e);e.type=HOST_INPUT_TEXT;memcpy(e.text,text_buf,sizeof e.text);if(host_elf_test_text(e.text)>0)atomic_store_explicit(&text_state,2,memory_order_release);}
}
void sc_ctl_frame_presented(int pal_set) {
    if(!atomic_load_explicit(&enabled,memory_order_relaxed))return;
    atomic_fetch_add(&presented_count,1);atomic_store(&palette_present,pal_set);
}
void sc_ctl_stop(void) {
    atomic_store(&enabled,0);atomic_store(&running,0);if(unix_fd>=0){close(unix_fd);unix_fd=-1;}if(tcp_fd>=0){close(tcp_fd);tcp_fd=-1;}if(unix_path[0]){unlink(unix_path);unix_path[0]=0;}
}
