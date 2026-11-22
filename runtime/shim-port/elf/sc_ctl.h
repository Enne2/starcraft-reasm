#ifndef SC_CTL_H
#define SC_CTL_H

/* In-process automation endpoint. Snapshot data is copied by the presentation thread. */
int sc_ctl_start(void);
void sc_ctl_present(const unsigned char *canvas, int width, int height, int pitch,
                    const unsigned int palette[256]);
void sc_ctl_frame_presented(int palette_set);
void sc_ctl_stop(void);

#endif
