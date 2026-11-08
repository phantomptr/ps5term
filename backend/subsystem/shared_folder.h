/* PS5Term GPL-3.0-or-later. Worker-only callbacks exposing one host directory to the guest.
 * Paths are relative to that directory ("" is the directory itself), never start with '/'
 * and contain no "." or ".." components. Handles are positive and stay valid until closed,
 * even if the file is removed or renamed. No asynchronous completion, engine reentry,
 * exceptions or longjmp across callbacks. Negative results are Linux/9p errno values,
 * independent of the native host errno numbering. Context is borrowed and must outlive the
 * worker, including failed joins. */
#ifndef PS5TERM_SHARED_FOLDER_H
#define PS5TERM_SHARED_FOLDER_H
#include <stddef.h>
#include <stdint.h>
#define PS5_SHARE_PATH 1024 /* Including the terminator. */
#define PS5_SHARE_NAME 256  /* Including the terminator. */
#define PS5_SHARE_IO 65536u
#define PS5_SHARE_READ 1
#define PS5_SHARE_WRITE 2
#define PS5_SHARE_CREATE 4
#define PS5_SHARE_EXCLUSIVE 8
#define PS5_SHARE_TRUNCATE 16
typedef struct {
 uint64_t device, inode, size, blocks, mtime_sec;
 uint32_t mtime_nsec, mode, nlink; /* mode uses Linux type and permission bits */
} Ps5ShareStat;
typedef struct { uint64_t block_size, blocks, free, available, files, files_free; } Ps5ShareSpace;
typedef struct {
 void *context;
 int (*stat)(void *,const char *,Ps5ShareStat *); /* Does not follow a final symlink. */
 int (*fstat)(void *,unsigned,Ps5ShareStat *);
 int (*open)(void *,const char *,int flags,unsigned mode); /* Regular files only. */
 void (*close)(void *,unsigned);
 int (*read)(void *,unsigned,uint64_t,void *,size_t);
 int (*write)(void *,unsigned,uint64_t,const void *,size_t);
 int (*resize)(void *,unsigned handle,const char *path,uint64_t); /* path when handle is 0 */
 int (*chmod)(void *,const char *,unsigned);
 int (*times)(void *,const char *,uint64_t a_sec,uint32_t a_nsec,uint64_t m_sec,uint32_t m_nsec,int now);
 int (*mkdir)(void *,const char *,unsigned);
 int (*remove)(void *,const char *); /* A file or an empty directory. */
 int (*rename)(void *,const char *,const char *);
 int (*readlink)(void *,const char *,char *,size_t);
 /* Writes entry number `cookie` of the directory: 1=entry, 0=end. `key` names a reader, so
  * reading on from the previous entry is cheap and stays correct while entries are removed. */
 int (*list)(void *,unsigned key,const char *,uint64_t cookie,char *,size_t);
 void (*list_close)(void *,unsigned key);
 int (*space)(void *,Ps5ShareSpace *);
} Ps5LinuxShare;
#endif
