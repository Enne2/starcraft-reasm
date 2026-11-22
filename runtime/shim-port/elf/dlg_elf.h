/* Native Win32 dialogs; all handles/parameters below are i386 values. */
#ifndef SC_DLG_ELF_H
#define SC_DLG_ELF_H
#define DLG_S __attribute__((stdcall))
typedef int (DLG_S *DlgProc)(unsigned int, unsigned int, unsigned int, unsigned int);
DLG_S int DialogBoxParamA(void *, const char *, unsigned int, DlgProc, unsigned int);
DLG_S int EndDialog(unsigned int, int);
DLG_S int DefDlgProcA(unsigned int, unsigned int, unsigned int, unsigned int);
DLG_S int IsDialogMessageA(unsigned int, void *);
DLG_S int SendDlgItemMessageA(unsigned int, int, unsigned int, unsigned int, unsigned int);
DLG_S int MapDialogRect(unsigned int, int *);
DLG_S unsigned int GetNextDlgTabItem(unsigned int, unsigned int, int);
DLG_S unsigned int GetNextDlgGroupItem(unsigned int, unsigned int, int);
void *dlg_elf_lookup(const char *);
/* Isolated native regression suite, called only for SC_DLG_SELFTEST=1. */
int dlg_elf_selftest(void);
/* Opt-in interactive probes; never selected during normal game startup. */
int dlg_elf_input_demo(void);
int dlg_elf_input_selftest(void);
/* Bounded template entry also used by the regression harness. */
int dlg_elf_template(void *, const void *, unsigned int, unsigned int, DlgProc,
                     unsigned int, const char *);
/* UI integration: real default wndproc, destruction notification, repaint. */
DLG_S int dlg_elf_wndproc(unsigned int, unsigned int, unsigned int, unsigned int);
void dlg_elf_destroyed(unsigned int);
void dlg_elf_dirty(unsigned int);
#endif
