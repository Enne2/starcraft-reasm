#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint32_t u32;
typedef int32_t i32;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
#define COM __attribute__((stdcall))

static unsigned char canvas[640 * 480];
static int host_w = 640, host_h = 480, host_pitch = 640;
void *host_elf_canvas(int *w, int *h, int *p) { *w=host_w; *h=host_h; *p=host_pitch; return canvas; }
int host_elf_display_mode(int w,int h,int b) { (void)b; host_w=w; host_h=h; host_pitch=w; return 0; }
void host_elf_blit8(unsigned char *d,int dp,int dw,int dh,const unsigned char *s,int sp,int sw,int sh,const int *dr,const int *sr,int ck) {
 int x,y; int sx0=sr?sr[0]:0, sy0=sr?sr[1]:0, sx1=sr?sr[2]:sw, sy1=sr?sr[3]:sh;
 int dx0=dr?dr[0]:0, dy0=dr?dr[1]:0, dx1=dr?dr[2]:dw, dy1=dr?dr[3]:dh;
 for(y=dy0;y<dy1;y++) for(x=dx0;x<dx1;x++) {
  if(x<0||y<0||x>=dw||y>=dh) continue;
  int sx=sx0+(x-dx0)*(sx1-sx0)/(dx1-dx0), sy=sy0+(y-dy0)*(sy1-sy0)/(dy1-dy0);
  if(sx<0||sy<0||sx>=sw||sy>=sh) continue;
  unsigned char v=s[sy*sp+sx]; if(ck<0||v!=(unsigned char)ck) d[y*dp+x]=v;
 }
}
void host_elf_fill8(unsigned char*d,int p,int w,int h,const int*r,int c) { int x,y; for(y=r[1];y<r[3];y++)for(x=r[0];x<r[2];x++)if(x>=0&&y>=0&&x<w&&y<h)d[y*p+x]=c; }
void host_elf_present(void){} void host_elf_set_palette(const u32*p,int a,int n){(void)p;(void)a;(void)n;}
void host_elf_get_palette(u32*p,int a,int n){(void)p;(void)a;(void)n;}
void host_elf_coop_level(unsigned long h,u32 f){(void)h;(void)f;} void host_elf_delay(unsigned n){(void)n;}
unsigned long host_elf_ticks(void){return 0;} void host_elf_shutdown(void){}
void reasm_elf_log(const char*s){(void)s;} void reasm_elf_log_hex(unsigned v){(void)v;}
void arm_com_wrap_vtable(void **v,const unsigned char*a,unsigned n){(void)v;(void)a;(void)n;}

extern long __attribute__((stdcall)) DirectDrawCreate(const void*,void**,void*);
extern long __attribute__((stdcall)) DirectDrawCreateClipper(unsigned long,void**,void*);
extern void *COM CreateDIBitmap(void*,const void*,unsigned long,const void*,const void*,unsigned);
extern int COM SetDIBits(void*,void*,unsigned,unsigned,const void*,const void*,unsigned);
extern long COM GetBitmapBits(void*,long,void*);
extern int COM DeleteObject(void*);

static void test_clipper(void) {
 void *dd=0,*src=0,*dst=0,*clip=0,*got=0; void **dv,**sv,**cv; unsigned char sb[16],db[16];
 unsigned char desc[124]; int changed=0; u32 region[12]; u32 size; int i;
 CHECK(DirectDrawCreate(0,&dd,0)==0); dv=*(void***)dd;
 memset(desc,0,sizeof desc); *(u32*)desc=124; *(u32*)(desc+4)=0x1006; /* DDSD_HEIGHT|WIDTH|PIXELFORMAT */ *(u32*)(desc+8)=4; *(u32*)(desc+12)=4; *(u32*)(desc+0x48)=32; *(u32*)(desc+0x48+12)=8;
 CHECK(((long(COM *)(void*,void*,void**,void*))dv[6])(dd,desc,&src,0)==0);
 CHECK(((long(COM *)(void*,void*,void**,void*))dv[6])(dd,desc,&dst,0)==0);
 sv=*(void***)src; memset(sb,1,sizeof sb); for(i=0;i<16;i++) sb[i]=(unsigned char)(i+1);
 /* Lock surface desc to inject/query memory. */
 { unsigned char sd[124]; memset(sd,0,sizeof sd); *(u32*)sd=124; ((long(COM *)(void*,void*,void*,u32,void*))sv[25])(src,0,sd,0,0); memcpy(*(void**)(sd+0x24),sb,16); }
 { void **tv=*(void***)dst; unsigned char sd[124]; memset(sd,0,sizeof sd); *(u32*)sd=124; ((long(COM *)(void*,void*,void*,u32,void*))tv[25])(dst,0,sd,0,0); memset(*(void**)(sd+0x24),0xEE,16); }
 CHECK(DirectDrawCreateClipper(0,&clip,0)==0); cv=*(void***)clip;
 memset(region,0,sizeof region); region[0]=32; region[1]=1; region[2]=1; region[3]=16; region[4]=0; region[5]=0; region[6]=0; region[7]=0; region[8]=1; region[9]=1; region[10]=3; region[11]=3;
 CHECK(((long(COM *)(void*,void*,u32))cv[7])(clip,region,0)==0);
 CHECK(((long(COM *)(void*,int*))cv[6])(clip,&changed)==0 && changed==1);
 CHECK(((long(COM *)(void*,int*))cv[6])(clip,&changed)==0 && changed==0);
 size=0; CHECK(((long(COM *)(void*,void*,void*,u32*))cv[3])(clip,0,0,&size)==(long)0x887600E4u && size==48);
 { u32 copy[12]; u32 cap=sizeof copy; CHECK(((long(COM *)(void*,void*,void*,u32*))cv[3])(clip,0,copy,&cap)==0 && !memcmp(copy,region,48)); }
 CHECK(((long(COM *)(void*,void*))(*(void***)dst)[28])(dst,clip)==0);

 { void **tv=*(void***)dst; unsigned char sd[124]; int dr[4]={0,0,4,4}; memset(sd,0,sizeof sd); *(u32*)sd=124; ((long(COM *)(void*,void*,void*,u32,void*))tv[25])(dst,0,sd,0,0); memset(*(void**)(sd+0x24),0xEE,16);
   CHECK(((long(COM *)(void*,void*,void*,void*,u32,void*))tv[5])(dst,dr,src,0,0,0)==0);
   unsigned char *p=*(unsigned char**)(sd+0x24); for(i=0;i<16;i++){ int x=i%4,y=i/4; int inside=(x>=1&&x<3&&y>=1&&y<3); CHECK(p[i]==(inside?sb[i]:0xEE)); }
   CHECK(((long(COM *)(void*,void**))tv[15])(dst,&got)==0 && got==clip);
 }
 /* BltFast must obey the same clip list. */
 { void **tv=*(void***)dst; unsigned char sd[124]; int x,y; memset(sd,0,sizeof sd); *(u32*)sd=124; ((long(COM *)(void*,void*,void*,u32,void*))tv[25])(dst,0,sd,0,0); memset(*(void**)(sd+0x24),0xEE,16); CHECK(((long(COM *)(void*,u32,u32,void*,void*,u32))tv[7])(dst,0,0,src,0,0)==0); unsigned char*p=*(unsigned char**)(sd+0x24); for(y=0;y<4;y++)for(x=0;x<4;x++) CHECK(p[y*4+x]==((x>=1&&x<3&&y>=1&&y<3)?sb[y*4+x]:0xEE)); }
 puts("PASS clipper Set/Get/changed, surface attach/get, Blt/BltFast pixel clipping");
}

static void test_rop3(void) {
 void *dd=0,*src=0,*dst=0; void **dv,**sv,**tv; unsigned char desc[124],ss[124],ds[124],fx[100];
 unsigned char *sp,*dp; int sr[4]={0,0,2,1},dr[4]={0,0,2,1};
 u32 rops[]={0x00CC0020u,0x008800C6u,0x00EE0086u,0x00660046u,0x00F00021u};
 unsigned char expected[5]; unsigned i;
 CHECK(DirectDrawCreate(0,&dd,0)==0); dv=*(void***)dd;
 memset(desc,0,sizeof desc); *(u32*)desc=124; *(u32*)(desc+4)=0x1006; *(u32*)(desc+8)=1; *(u32*)(desc+12)=2; *(u32*)(desc+0x48)=32; *(u32*)(desc+0x48+12)=8;
 CHECK(((long(COM *)(void*,void*,void**,void*))dv[6])(dd,desc,&src,0)==0);
 CHECK(((long(COM *)(void*,void*,void**,void*))dv[6])(dd,desc,&dst,0)==0);
 sv=*(void***)src; tv=*(void***)dst;
 memset(ss,0,sizeof ss); memset(ds,0,sizeof ds); *(u32*)ss=*(u32*)ds=124;
 CHECK(((long(COM *)(void*,void*,void*,u32,void*))sv[25])(src,0,ss,0,0)==0);
 CHECK(((long(COM *)(void*,void*,void*,u32,void*))tv[25])(dst,0,ds,0,0)==0);
 sp=*(unsigned char**)(ss+0x24); dp=*(unsigned char**)(ds+0x24); sp[0]=0x3C; sp[1]=0xA5; dp[0]=0x5A; dp[1]=0x0F;
 expected[0]=0x3C; expected[1]=(unsigned char)(0x3C&0x5A); expected[2]=(unsigned char)(0x3C|0x5A); expected[3]=(unsigned char)(0x3C^0x5A); expected[4]=0xC3;
 for(i=0;i<5;i++) {
  memset(fx,0,sizeof fx); *(u32*)(fx+0)=100; *(u32*)(fx+4)=0x1000; *(u32*)(fx+8)=rops[i]; *(u32*)(fx+0x50)=0xC3;
  dp[0]=0x5A; dp[1]=0x0F;
  CHECK(((long(COM *)(void*,void*,void*,void*,u32,void*))tv[5])(dst,dr,src,sr,0x40,fx)==0);
  CHECK(dp[0]==expected[i]);
 }
 puts("PASS DDBLT_ROP ROP3 SRCCOPY/SRCAND/SRCPAINT/SRCINVERT/PATCOPY with source, destination, pattern");
}

static void init_bmi(unsigned char *bmi,int w,int h,int bpp,u32 used) { memset(bmi,0,40+1024); *(u32*)(bmi+0)=40; *(u32*)(bmi+4)=w; *(u32*)(bmi+8)=h; *(unsigned short*)(bmi+12)=1; *(unsigned short*)(bmi+14)=bpp; *(u32*)(bmi+32)=used; }
static void test_dib(void) {
 unsigned char bmi8[1064],bmi24[1064],bmi32[1064],bits24[24],bits32[32],bits8[8],out[64]; void *bm8,*bm24; int i;
 init_bmi(bmi8,3,2,8,2); /* RGBQUAD B,G,R,reserved: black, red */ bmi8[44]=0; bmi8[45]=0; bmi8[46]=255;
 init_bmi(bmi24,3,2,24,0); init_bmi(bmi32,3,2,32,0);
 memset(bits24,0xA5,sizeof bits24); /* 3 pixels*3 + 3-byte stride padding */ bits24[0]=0;bits24[1]=0;bits24[2]=255; bits24[3]=0;bits24[4]=255;bits24[5]=0; bits24[6]=255;bits24[7]=0;bits24[8]=0;
 bits24[12]=0;bits24[13]=0;bits24[14]=0; bits24[15]=255;bits24[16]=255;bits24[17]=255; bits24[18]=0;bits24[19]=0;bits24[20]=255;
 bm8=CreateDIBitmap(0,bmi8,0,0,bmi8,0); CHECK(bm8);
 CHECK(SetDIBits(0,bm8,0,2,bits24,bmi24,0)==2); memset(out,0,sizeof out); CHECK(GetBitmapBits(bm8,sizeof out,out)==8);
 /* RGB -> nearest in [black, red], fallback greys for remaining palette entries. */ CHECK(out[0]==1); CHECK(out[1]==85); CHECK(out[2]==85); CHECK(out[4]==0); CHECK(out[5]==255); CHECK(out[6]==1);
 for(i=3;i<4;i++) CHECK(out[i]==0); /* pitch padding remains untouched/zero */
 init_bmi(bmi24,3,2,24,0); bm24=CreateDIBitmap(0,bmi24,0,0,bmi24,0); CHECK(bm24);
 init_bmi(bmi8,3,2,8,2); bmi8[40+4+2]=255; /* index 1 red */
 bits8[0]=1;bits8[1]=0;bits8[2]=1;bits8[3]=0;bits8[4]=0;bits8[5]=1;bits8[6]=0;bits8[7]=0;
 CHECK(SetDIBits(0,bm24,0,2,bits8,bmi8,0)==2); memset(out,0,sizeof out); CHECK(GetBitmapBits(bm24,sizeof out,out)==24);
 CHECK(out[0]==0&&out[1]==0&&out[2]==255); CHECK(out[3]==0&&out[4]==0&&out[5]==0); CHECK(out[6]==0&&out[7]==0&&out[8]==255);
 /* 8->32 and 32->8, with an empty palette fallback. */
 init_bmi(bmi32,3,2,32,0); { void *bm32=CreateDIBitmap(0,bmi32,0,0,bmi32,0); unsigned char b32[32]={0}; b32[0]=10;b32[1]=20;b32[2]=30; b32[4]=40;b32[5]=50;b32[6]=60; b32[8]=70;b32[9]=80;b32[10]=90; CHECK(SetDIBits(0,bm32,0,2,bits8,bmi8,0)==2); CHECK(SetDIBits(0,bm32,0,2,b32,bmi32,0)==2); CHECK(DeleteObject(bm32)); }
 CHECK(DeleteObject(bm8)); CHECK(DeleteObject(bm24)); puts("PASS SetDIBits 8/24/32, RGBQUAD, empty-table fallback, DWORD stride");
}
int main(void){test_clipper();test_dib();test_rop3();puts("x86_gfx_test: PASS");return 0;}
