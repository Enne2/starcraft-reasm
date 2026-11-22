#include "sc_ctl.h"
#include "host_elf.h"
#include <string.h>
#include <time.h>
#include <unistd.h>

int host_elf_test_event(const struct host_input_event *e) { (void)e; return 1; }
int host_elf_test_text(const char *s) { (void)s; return 1; }

int main(void) {
    static unsigned char canvas[640*480];
    static unsigned int palette[256];
    struct timespec delay={0,1000000};
    int i;
    memset(canvas,1,sizeof canvas); palette[1]=0x123456;
    if(sc_ctl_start()!=0)return 2;
    for(i=0;i<15000;++i){sc_ctl_present(canvas,640,480,640,palette);sc_ctl_frame_presented(1);nanosleep(&delay,0);}
    sc_ctl_stop();
    return 0;
}
