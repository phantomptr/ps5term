// PS5Term - GPL-3.0-or-later. Public libkernel directory ABI, no payload.
#include "native_sftp_directory.h"
#include "src/platform/directory.hpp"
#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <new>
#include <sys/stat.h>
#include <unistd.h>

struct ps5term_sftp_directory
{
    int descriptor=-1;
    std::array<char,65536> buffer{};
    std::size_t used=0,offset=0;
    long base=0;
    struct dirent entry{};
};
extern "C" ps5term_sftp_directory *ps5term_sftp_fdopendir(int fd)
{
    struct stat info{};
    if(fstat(fd,&info)!=0) return nullptr;
    if(!S_ISDIR(info.st_mode)) { errno=ENOTDIR;return nullptr; }
    void *memory=std::malloc(sizeof(ps5term_sftp_directory));
    if(!memory) { errno=ENOMEM;return nullptr; }
    auto *directory=new(memory) ps5term_sftp_directory;
    directory->descriptor=fd;
    return directory;
}
extern "C" ps5term_sftp_directory *ps5term_sftp_opendir(const char *path)
{
    const int fd=::open(path,O_RDONLY|O_DIRECTORY);
    if(fd<0) return nullptr;
    auto *directory=ps5term_sftp_fdopendir(fd);
    if(!directory) { const int error=errno;::close(fd);errno=error; }
    return directory;
}
extern "C" struct dirent *ps5term_sftp_readdir(ps5term_sftp_directory *directory)
{
    if(!directory) { errno=EBADF;return nullptr; }
    if(directory->offset==directory->used)
    {
        const auto size=getdirentries(directory->descriptor,directory->buffer.data(),
                                      directory->buffer.size(),&directory->base);
        if(size<0) return nullptr;
        if(!size) { errno=0;return nullptr; }
        if(static_cast<std::size_t>(size)>directory->buffer.size()) { errno=EIO;return nullptr; }
        directory->used=size;directory->offset=0;
    }
    const auto record=term::platform::decode_directory_record(
        {directory->buffer.data()+directory->offset,directory->used-directory->offset});
    if(!record.valid || record.name.size()>=sizeof(directory->entry.d_name))
    { errno=EIO;return nullptr; }
    directory->offset+=record.bytes;
    directory->entry={};
    std::memcpy(directory->entry.d_name,record.name.data(),record.name.size());
    return &directory->entry;
}
extern "C" int ps5term_sftp_closedir(ps5term_sftp_directory *directory)
{
    if(!directory) { errno=EBADF;return -1; }
    const int result=::close(directory->descriptor);
    directory->~ps5term_sftp_directory();std::free(directory);
    return result;
}
extern "C" void ps5term_sftp_rewinddir(ps5term_sftp_directory *directory)
{
    if(!directory) { errno=EBADF;return; }
    if(lseek(directory->descriptor,0,SEEK_SET)<0) return;
    directory->used=directory->offset=0;directory->base=0;
}
