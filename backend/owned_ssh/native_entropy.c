/* PS5Term native entropy adapter. GPL-3.0-or-later.
 * Kernel-backed device only; any failed/short read fails closed. No libc PRNG,
 * timestamp, hardware-instruction fallback or payload service is involved. */
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
int ps5term_native_seed(unsigned char *output,unsigned int size)
{
    if(!output || !size || size>1024) return -1;
    const int fd=open("/dev/random",O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
    if(fd<0) return -1;
    struct stat info={0};
    if(fstat(fd,&info)!=0 || !S_ISCHR(info.st_mode))
    { close(fd); return -1; }
    unsigned int offset=0;
    while(offset<size)
    {
        const ssize_t count=read(fd,output+offset,size-offset);
        if(count<0 && errno==EINTR) continue;
        if(count<=0) break;
        offset+=(unsigned int)count;
    }
    const int closed=close(fd);
    if(offset==size && closed==0) return 0;
    volatile unsigned char *wipe=output;
    for(unsigned int i=0;i<size;++i) wipe[i]=0;
    return -1;
}
