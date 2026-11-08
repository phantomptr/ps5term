// Host-only OS entropy adapter. GPL-3.0-or-later.
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif
#include <errno.h>
#include <sys/random.h>
#include <unistd.h>
int ps5term_host_seed(unsigned char *output, unsigned int size)
{
    if (!output)
        return -1;
    while (size)
    {
        const unsigned int count = size > 256 ? 256 : size;
        if (getentropy(output, count) != 0)
        {
            if (errno == EINTR)
                continue;
            return -1;
        }
        output += count;
        size -= count;
    }
    return 0;
}
