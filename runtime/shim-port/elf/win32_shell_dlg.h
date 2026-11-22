#ifndef SC_WIN32_SHELL_DLG_H
#define SC_WIN32_SHELL_DLG_H
#if defined(__i386__)
#define SC_SHELL_STDCALL __attribute__((stdcall))
#else
#define SC_SHELL_STDCALL
#endif
typedef struct {
    unsigned int lStructSize, hwndOwner, hInstance;
    const char *lpstrFilter; char *lpstrCustomFilter;
    unsigned int nMaxCustFilter, nFilterIndex; char *lpstrFile;
    unsigned int nMaxFile; char *lpstrFileTitle; unsigned int nMaxFileTitle;
    const char *lpstrInitialDir; const char *lpstrTitle;
    unsigned int Flags; unsigned short nFileOffset, nFileExtension;
    const char *lpstrDefExt; unsigned long lCustData;
    void *lpfnHook; const char *lpTemplateName;
} SC_OPENFILENAMEA;
int SC_SHELL_STDCALL GetSaveFileNameA(SC_OPENFILENAMEA *);
int SC_SHELL_STDCALL GetOpenFileNameA(SC_OPENFILENAMEA *);
int SC_SHELL_STDCALL SHGetSpecialFolderLocation(void *, int, void **);
int SC_SHELL_STDCALL SHGetPathFromIDListA(const void *, char *);
int SC_SHELL_STDCALL ShellExecuteA(void *, const char *, const char *, const char *, const char *, int);
unsigned int SC_SHELL_STDCALL FindExecutableA(const char *, const char *, char *);
#endif
