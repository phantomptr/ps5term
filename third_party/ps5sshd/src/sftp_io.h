#pragma once

/* Force-included when compiling wolfSSH's wolfsftp.c. The system headers are
   pulled in first so their declarations stay untouched, then the file
   descriptor operations used for SFTP handles are routed through sftp_io.c,
   which batches sequential writes (the PS5 filesystem is very slow with small
   writes: ~14 MB/s at 32 KiB vs ~190 MB/s at 4 MiB). */

#include <dirent.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

int     sftpio_pwrite(int fd, unsigned char *buf, unsigned int sz,
                   const unsigned int *shortOffset);
int     sftpio_pread(int fd, unsigned char *buf, unsigned int sz,
                  const unsigned int *shortOffset);
int     sftpio_close(int fd);
int     sftpio_fstat(int fd, struct stat *st);
int     sftpio_ftruncate(int fd, off_t len);
int     sftpio_fchmod(int fd, mode_t mode);
int     sftpio_fchown(int fd, uid_t uid, gid_t gid);
ssize_t sftpio_write(int fd, const void *buf, size_t len);

/* Filesystem statistics for the statvfs@openssh.com and fstatvfs@openssh.com
   SFTP extensions, in protocol order: bsize, frsize, blocks, bfree, bavail,
   files, ffree, favail, fsid, flag, namemax. Uses path if fd is negative. */
#define SFTPIO_STATVFS_FIELDS 11
int     sftpio_statvfs(const char *path, int fd,
                       uint64_t out[SFTPIO_STATVFS_FIELDS]);

#define wPwrite   sftpio_pwrite
#define wPread    sftpio_pread
#define close     sftpio_close
#define fstat     sftpio_fstat
#define ftruncate sftpio_ftruncate
#define fchmod    sftpio_fchmod
#define fchown    sftpio_fchown
#define write     sftpio_write
