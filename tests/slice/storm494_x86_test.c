typedef unsigned char u8;
extern u8 *__attribute__((stdcall)) storm494(u8 *, int) __asm__("_storm494");
int mainCRTStartup(void) {
  u8 buf[8] = {0x71,0x72,0x73,0x74,0x75,0x76,0x77,0x78};
  if (storm494(buf, 5) != buf + 5) return 1;
  for (int i=0; i<5; ++i) if (buf[i] != 0) return 2;
  if (buf[5] != 0x76 || buf[6] != 0x77) return 3;
  if (storm494(buf+2, 0) != buf+2) return 4;
  return 0;
}
