#include "../runtime/shim-port/elf/win32_shell_dlg.h"
extern int setenv(const char *,const char *,int);
extern int strcmp(const char *,const char *);
extern int unsetenv(const char *);
extern void *malloc(unsigned int);
extern void free(void *);
extern int printf(const char *,...);
extern int puts(const char *);
int main(void) {
    void *pidl=0; char path[260], file[260]={0};
    SC_OPENFILENAMEA o={0};
    setenv("SC_ROOT","build/qa/gap-win32/sandbox",1);
    if (SHGetSpecialFolderLocation(0,5,&pidl)!=0 || !SHGetPathFromIDListA(pidl,path)) return 1;
    if (strcmp(path,"build/qa/gap-win32/sandbox/Save")) return 2;
    o.lStructSize=sizeof(o); o.lpstrFile=file; o.nMaxFile=sizeof(file); o.lpstrDefExt="sav";
    setenv("SC_SAVE_FILE","test",1);
    if (!GetSaveFileNameA(&o) || strcmp(file,"test.sav")) return 3;
    unsetenv("SC_SAVE_FILE");
    if (GetSaveFileNameA(&o)) return 4; /* no UI path unless explicit auto mode */
    setenv("SC_SAVE_AUTO","1",1);
    o.lpstrDefExt="pcx";
    if (GetSaveFileNameA(&o)) return 5; /* never redirect Storm's screenshot dialog */
    o.lpstrDefExt="sav";
    if (!GetSaveFileNameA(&o) || strcmp(file,"Save/autosave.sav")) return 6;
    file[0]=0;
    if (!GetOpenFileNameA(&o) || strcmp(file,"Save/autosave.sav")) return 7;
    if (ShellExecuteA(0,"open","x",0,0,1)!=2 || FindExecutableA("x",0,0)!=2) return 8;
    puts("x86_saveload_test: PASS"); return 0;
}
