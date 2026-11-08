/* PS5Term - GPL-3.0-or-later. Opt-in, bounded startup phase diagnostics only. */
#ifndef PS5TERM_GUEST_TRACE_H
#define PS5TERM_GUEST_TRACE_H
#include <stddef.h>
#if defined(PS5TERM_LINUX_TRACE)
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
static inline void ps5_guest_trace(const char *phase,size_t size)
{
    int fd=open("/data/ps5term/linux-startup.log",O_WRONLY|O_CREAT|O_APPEND|O_NOFOLLOW|O_NONBLOCK,0644);
    if(fd<0) return;
    struct stat st;
    if(fstat(fd,&st)==0 && S_ISREG(st.st_mode) && st.st_size<16384) {
        char text[128];int n=snprintf(text,sizeof text,"%s %zu\n",phase,size);
        if(n>0 && (size_t)n<sizeof text) (void)write(fd,text,(size_t)n);
    }
    close(fd);
}
#else
static inline void ps5_guest_trace(const char *phase,size_t size) { (void)phase;(void)size; }
#endif
#endif
