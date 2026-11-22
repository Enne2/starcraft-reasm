#include "win32_shell_dlg.h"
#define S __attribute__((stdcall))
extern char *getenv(const char *);
extern int snprintf(char *, unsigned long, const char *, ...);
extern unsigned long strlen(const char *);
extern int mkdir(const char *, unsigned int);
extern long write(int, const void *, unsigned long);
extern int access(const char *, int);
extern void *memset(void *, int, unsigned long);
extern char *strchr(const char *, int);
extern int strcmp(const char *, const char *);

/* PIDLs are opaque stable tokens in this process; each maps to a sandbox path. */
static const char *folder_path(int csidl) {
    switch (csidl & 0xff) {
    case 5: return "Save";       /* CSIDL_PERSONAL: game save data */
    case 26: return "AppData";
    case 2: return "Programs";
    case 0: return ".";
    default: return 0;
    }
}
static char g_paths[4][512];
static int g_next;
int S SHGetSpecialFolderLocation(void *hwnd, int csidl, void **pidl) {
    const char *sub=folder_path(csidl); char root[384]; (void)hwnd;
    if (!pidl || !sub) return -1;
    const char *env=getenv("SC_ROOT"); if (!env || !*env) env=".";
    int nr=snprintf(root,sizeof root,"%s",env);
    if (nr<0 || (unsigned long)nr>=sizeof root) return -1;
    nr=snprintf(g_paths[g_next],sizeof g_paths[0],"%s/%s",root,sub);
    if (nr<0 || (unsigned long)nr>=sizeof g_paths[0]) return -1;
    mkdir(root,0755); mkdir(g_paths[g_next],0755);
    *pidl=g_paths[g_next]; g_next=(g_next+1)&3; return 0;
}
int S SHGetPathFromIDListA(const void *pidl, char *out) {
    const char *p=(const char *)pidl; unsigned long n;
    if (!p || !out) return 0; n=strlen(p); if (n>=260) return 0;
    __builtin_memcpy(out,p,n+1); return 1;
}
static int select_file(SC_OPENFILENAMEA *o, const char *var) {
    const char *p; unsigned long n; char tmp[1024];
    if (!o || o->lStructSize < sizeof(*o) || !o->lpstrFile || !o->nMaxFile) return 0;
    p=getenv(var);
    if (!p || !*p) {
        const char *automatic=getenv("SC_SAVE_AUTO");
        const char *ext=o->lpstrDefExt;
        if (!automatic || strcmp(automatic,"1") || !ext) return 0;
        /* Do not redirect unrelated dialogs (Storm's GetSaveFileNameA call is PCX screenshot). */
        if (!strcmp(ext,"sav") || !strcmp(ext,".sav")) p="Save/autosave.sav";
        else if (!strcmp(ext,"rep") || !strcmp(ext,".rep")) p="Maps/replays/auto.rep";
        else return 0;
    }
    n=strlen(p); if (n>=sizeof tmp || n>=o->nMaxFile) return 0;
    __builtin_memcpy(tmp,p,n+1);
    if (!strchr(tmp,'.') && o->lpstrDefExt && *o->lpstrDefExt) {
        int nr=snprintf(tmp+n,sizeof(tmp)-n,".%s",o->lpstrDefExt);
        if (nr<0 || (unsigned long)nr>=sizeof(tmp)-n || strlen(tmp)>=o->nMaxFile) return 0;
    }
    __builtin_memcpy(o->lpstrFile,tmp,strlen(tmp)+1); return 1;
}
int S GetSaveFileNameA(SC_OPENFILENAMEA *o) { return select_file(o,"SC_SAVE_FILE"); }
int S GetOpenFileNameA(SC_OPENFILENAMEA *o) { return select_file(o,"SC_LOAD_FILE"); }
int S ShellExecuteA(void *hwnd,const char *op,const char *file,const char *args,const char *dir,int show) {
    const char msg[]="win32_shell_dlg: ShellExecuteA blocked (no host execution)\n"; (void)hwnd;(void)op;(void)file;(void)args;(void)dir;(void)show; write(2,msg,sizeof msg-1); return 2;
}
unsigned int S FindExecutableA(const char *file,const char *dir,char *out) {
    const char msg[]="win32_shell_dlg: FindExecutableA unsupported\n"; (void)file;(void)dir;(void)out; write(2,msg,sizeof msg-1); return 2;
}
