typedef unsigned int u32;
__declspec(dllimport) int storm_probe(void);
__declspec(dllexport) int fixture_entry(u32 x) {
    /* Ordinal #1 import is the exercised external call. */
    return storm_probe() + (int)(x * 3u + 7u);
}
int mainCRTStartup(void) { return fixture_entry(5u) == 27 ? 0 : 1; }
