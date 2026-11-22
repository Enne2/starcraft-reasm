typedef unsigned int u32;
typedef struct {
  u32 eax, ecx, edx, ebx, esp, ebp, esi, edi, eip, eflags, cs, ss;
  double fpu[8];
  unsigned long long reserved;
} cpu_state;
extern void c_fixture_entry(cpu_state *);
static cpu_state cpu;
static u32 guest_stack[64];
__attribute__((noreturn)) void _start(void) {
  cpu.esp = (u32)(guest_stack + 10);
  guest_stack[10] = 5; /* EBP+8 after the generated prologue */
  c_fixture_entry(&cpu);
  register int status __asm__("r0") = cpu.eax == 27 ? 0 : 1;
  register int syscall __asm__("r7") = 1;
  __asm__ volatile("svc 0" : : "r"(status), "r"(syscall) : "memory");
  __builtin_unreachable();
}
