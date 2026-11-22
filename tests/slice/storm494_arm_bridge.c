typedef unsigned int u32;
typedef unsigned char u8;
typedef struct {
  u32 eax, ecx, edx, ebx, esp, ebp, esi, edi, eip, eflags, cs, ss;
  double fpu[8];
  unsigned long long reserved;
} cpu_state;
extern void c_ordinal_73746f726d2e646c6c_494(cpu_state *);
static cpu_state cpu;
static u32 guest_stack[64];
static u8 buffer[8];
__attribute__((noreturn)) void _start(void) {
  buffer[0]=0x71; buffer[1]=0x72; buffer[2]=0x73; buffer[3]=0x74;
  buffer[4]=0x75; buffer[5]=0x76; buffer[6]=0x77; buffer[7]=0x78;
  guest_stack[16]=(u32)buffer;
  guest_stack[17]=5;
  cpu.esp=(u32)&guest_stack[16];
  c_ordinal_73746f726d2e646c6c_494(&cpu);
  int status=0;
  if (cpu.eax != (u32)(buffer+5)) status=1;
  for (int i=0; i<5; ++i) if (buffer[i] != 0) status=2;
  if (buffer[5]!=0x76 || buffer[6]!=0x77) status=3;
  guest_stack[16]=(u32)(buffer+2);
  guest_stack[17]=0;
  cpu.esp=(u32)&guest_stack[16];
  c_ordinal_73746f726d2e646c6c_494(&cpu);
  if (cpu.eax != (u32)(buffer+2)) status=4;
  register int r0 __asm__("r0")=status;
  register int r7 __asm__("r7")=1;
  __asm__ volatile("svc 0" : : "r"(r0), "r"(r7) : "memory");
  __builtin_unreachable();
}
