/* Native in-process replacement for the standard.snp module. Battle.net is intentionally absent. */
#ifndef SC_SNP_ELF_H
#define SC_SNP_ELF_H

void *win32_snp_load_library(const char *name);
void *win32_snp_get_proc(void *module, const char *name);

/* caps.dat del provider servito come archivio MPQ in memoria (nessun file di gioco modificato).
 * Le API qui sotto sono chiamate dai punti di ingresso file di win32_shim.c: si applicano SOLO
 * all'handle virtuale del nostro modulo, mai ad altri file. */
void *win32_snp_vfile_open(const char *path);
int   win32_snp_vfile_own(void *handle);
unsigned int win32_snp_vfile_size(void *handle);
long  win32_snp_vfile_seek(void *handle, long offset, unsigned int method);
long  win32_snp_vfile_read(void *handle, void *buf, unsigned int bytes);
void  win32_snp_vfile_close(void *handle);

/* Solo per il gate: blob MPQ generato (caps.dat con id 0). */
const unsigned char *snp_elf_caps_mpq(unsigned int *bytes);

/* Focused gate helpers for a real socket created by spiInitialize. */
int snp_elf_test_socket_address(void *out, unsigned int bytes);
int snp_elf_test_socket_roundtrip(void);

#endif
