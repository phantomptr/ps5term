#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/stat.h>

#include <ps5/klog.h>

#include "ps5sshd.h"


typedef struct notify_request {
  char useless1[45];
  char message[3075];
} notify_request_t;

int sceKernelSendNotificationRequest(int, notify_request_t*, size_t, int);


static int g_logfd = -1;


void
log_open(void) {
  if(g_logfd >= 0) {
    close(g_logfd);
  }
  g_logfd = open(PS5SSHD_LOG, O_WRONLY | O_CREAT | O_APPEND, 0644);
}


void
log_printf(const char *fmt, ...) {
  char msg[1024];
  char line[1200];
  struct tm tm;
  time_t now;
  va_list args;
  int len;

  va_start(args, fmt);
  vsnprintf(msg, sizeof(msg), fmt, args);
  va_end(args);

  now = time(NULL);
  localtime_r(&now, &tm);
  len = snprintf(line, sizeof(line), "%04d-%02d-%02d %02d:%02d:%02d [%d] %s\n",
                 tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
                 tm.tm_min, tm.tm_sec, getpid(), msg);
  if(len < 0) {
    return;
  }
  if(len >= (int)sizeof(line)) {
    len = sizeof(line) - 1;
  }

  klog_printf("ps5sshd: %s\n", msg);
  if(g_logfd >= 0) {
    write(g_logfd, line, len);
  }
}


void
notify(const char *fmt, ...) {
  notify_request_t req;
  va_list args;

  memset(&req, 0, sizeof(req));
  va_start(args, fmt);
  vsnprintf(req.message, sizeof(req.message), fmt, args);
  va_end(args);

  sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
}


int
read_file(const char *path, uint8_t **buf, size_t *len) {
  struct stat st;
  uint8_t *data;
  size_t off = 0;
  int fd;

  if((fd=open(path, O_RDONLY)) < 0) {
    return -1;
  }
  if(fstat(fd, &st) || st.st_size < 0 || st.st_size > 1024 * 1024) {
    close(fd);
    return -1;
  }
  if(!(data=malloc(st.st_size + 1))) {
    close(fd);
    return -1;
  }
  while(off < (size_t)st.st_size) {
    ssize_t n = read(fd, data + off, st.st_size - off);
    if(n < 0 && errno == EINTR) {
      continue;
    }
    if(n <= 0) {
      break;
    }
    off += n;
  }
  close(fd);

  data[off] = 0;
  *buf = data;
  *len = off;

  return 0;
}


int
write_file(const char *path, const void *buf, size_t len, int mode) {
  const uint8_t *data = buf;
  size_t off = 0;
  int fd;

  if((fd=open(path, O_WRONLY | O_CREAT | O_TRUNC, mode)) < 0) {
    return -1;
  }
  while(off < len) {
    ssize_t n = write(fd, data + off, len - off);
    if(n < 0 && errno == EINTR) {
      continue;
    }
    if(n <= 0) {
      close(fd);
      return -1;
    }
    off += n;
  }
  fsync(fd);
  close(fd);

  return 0;
}


int
set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);

  if(flags < 0) {
    return -1;
  }
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}
