/* T3: host events -> existing Win32 queue; no SDL or WndProc calls here. */
#ifndef INPUT_ELF_H
#define INPUT_ELF_H
#include "host_elf.h"
typedef struct input_elf_message { unsigned int message, wparam, lparam; } input_elf_message;
/* max 32 messages (ANSI text bytes); returns count. */
int input_elf_translate(const struct host_input_event *e, input_elf_message out[32]);
unsigned int input_elf_virtual_key(int sdl_key);
void win32_input_pump(void); /* owning thread only, posts, NEVER dispatches */
int input_elf_selftest(void);
#endif
