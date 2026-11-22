#ifndef SC_MPQ_DIAG_H
#define SC_MPQ_DIAG_H
/* Called during ordinal lookup; installs only once BOTH native IATs are
 * resolved. Redirects unnamed completion events, never patches original .text
 * or changes shared shim implementations. SC_MPQ_EVENT_FIX=0 enables A/B. */
void mpq_diag_install(void);
typedef struct { unsigned created, live, peak; } MpqDiagStats;
void mpq_diag_get_stats(MpqDiagStats *out);
#endif
