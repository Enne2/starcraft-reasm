/* T4: additive APIs only; Windows scalar types are always 32 bit here. */
#ifndef SC_WIN32_GAME_H
#define SC_WIN32_GAME_H
#if !defined(__i386__) && !defined(__arm__)
#error win32_game requires the ELF32 i386 runtime (or the ARMv6 runtime)
#endif
#if defined(__i386__)
#define GAME_STDCALL __attribute__((stdcall))
#else
/* ARMv6 port: AAPCS has no stdcall. The guest stack discipline is the import bridge's job
 * (runtime/arm/bridge_arm.c), never the host calling convention, so the attribute must vanish
 * rather than be silently ignored. */
#define GAME_STDCALL
#endif
typedef unsigned int game_u32;
typedef struct { int left, top, right, bottom; } GameRect;
/* Exact T3 weak-table ABI; the provider definition is strong. */
typedef struct { const char *dll; const char *name; void *fn; } Win32GameApi;
extern const Win32GameApi win32_extra_apis[];
int GAME_STDCALL SetPropA(game_u32 hwnd, const char *key, void *value);
void *GAME_STDCALL GetPropA(game_u32 hwnd, const char *key);
void *GAME_STDCALL RemovePropA(game_u32 hwnd, const char *key);
int GAME_STDCALL IntersectRect(GameRect *out, const GameRect *a, const GameRect *b);
int GAME_STDCALL VirtualLock(void *address, game_u32 size);
int GAME_STDCALL VirtualUnlock(void *address, game_u32 size);
int GAME_STDCALL IsDebuggerPresent(void);
/* Already-registered shim APIs: supervisor routes those bodies to these
 * backends. Prefixes avoid duplicate global symbols and hidden overrides. */
typedef struct { game_u32 lo, hi; } GameFileTime;
typedef struct {
    game_u32 attributes;
    GameFileTime creation, access, write;
    game_u32 size_hi, size_lo, reserved0, reserved1;
    char filename[260], alternate[14];
} GameFindData;
int GAME_STDCALL game_CreateDirectoryA(const char *path, void *security);
void *GAME_STDCALL game_FindFirstFileA(const char *pattern, GameFindData *data);
int GAME_STDCALL game_FindNextFileA(void *handle, GameFindData *data);
int GAME_STDCALL game_FindClose(void *handle);
#endif
