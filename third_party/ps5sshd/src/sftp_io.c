/* Write batching for SFTP file handles, see sftp_io.h.
 *
 * Sequential writes are collected into 4 MiB blocks. A full block is copied
 * into shared memory and handed to a separate writer process (forked from the
 * session on first use), so disk writes overlap with receiving the next block
 * from the network. The file descriptor travels with each job over a unix
 * socket (SCM_RIGHTS). Threads are avoided on purpose: they crashed the PS5
 * when used in the forked session process.
 *
 * Each handle has at most one block in flight; every other operation on the
 * handle waits for it first, which keeps writes ordered and lets reads/stats
 * see the data. Write errors are reported by the next operation on the handle
 * (at the latest on close). If anything about the writer process fails, the
 * code falls back to writing synchronously. Creating PS5SSHD_DIR/sync_writes
 * forces synchronous writes.
 */

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/mman.h>
#ifdef PS5SSHD_HOST
#include <sys/statvfs.h>
#else
#include <sys/mount.h>
#endif
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/wait.h>

#include "ps5sshd.h"

#define BATCH_SIZE  (4 * 1024 * 1024)
#define BATCH_SLOTS 8
#define SHM_BUFS    4
#define SHM_MAGIC   0x70733573u

#define ASYNC_UNTRIED  0
#define ASYNC_ON       1
#define ASYNC_OFF      2


typedef struct batch {
  int      used;
  int      fd;
  off_t    start;
  size_t   len;
  uint8_t *buf;
  int      inflight;
  int      error;
} batch_t;


typedef struct job {
  uint32_t bufidx;
  uint32_t slot;
  int64_t  off;
  uint64_t len;
} job_t;


typedef struct reply {
  uint32_t bufidx;
  uint32_t slot;
  int32_t  error;
} reply_t;


static batch_t  g_batches[BATCH_SLOTS];
static int      g_async = ASYNC_UNTRIED;
static int      g_sock = -1;
static pid_t    g_writer = -1;
static uint8_t *g_shm = NULL;
static int      g_shm_busy[SHM_BUFS];


/* ---------------------------------------------------------------------- */
/* helpers shared by both processes                                        */

static int
read_full(int fd, void *buf, size_t len) {
  uint8_t *p = buf;

  while(len) {
    ssize_t n = read(fd, p, len);
    if(n < 0 && errno == EINTR) {
      continue;
    }
    if(n <= 0) {
      return -1;
    }
    p += n;
    len -= n;
  }
  return 0;
}


static int
write_full(int fd, const void *buf, size_t len) {
  const uint8_t *p = buf;

  while(len) {
    ssize_t n = write(fd, p, len);
    if(n < 0 && errno == EINTR) {
      continue;
    }
    if(n <= 0) {
      return -1;
    }
    p += n;
    len -= n;
  }
  return 0;
}


/* Returns 0 or an errno value. */
static int
pwrite_full(int fd, const uint8_t *buf, size_t len, off_t off) {
  size_t done = 0;

  while(done < len) {
    ssize_t n = pwrite(fd, buf + done, len - done, off + done);

    if(n < 0 && errno == EINTR) {
      continue;
    }
    if(n <= 0) {
      return n == 0 ? EIO : errno;
    }
    done += n;
  }

  return 0;
}


/* ---------------------------------------------------------------------- */
/* writer process                                                          */

/* Receive one job and the descriptor attached to it. */
static int
recv_job(int sock, job_t *job, int *fd) {
  union {
    struct cmsghdr hdr;
    char buf[CMSG_SPACE(sizeof(int))];
  } ctrl;
  struct msghdr msg;
  struct iovec iov;
  struct cmsghdr *cmsg;
  ssize_t n;

  memset(&msg, 0, sizeof(msg));
  iov.iov_base = job;
  iov.iov_len = sizeof(*job);
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  msg.msg_control = ctrl.buf;
  msg.msg_controllen = sizeof(ctrl.buf);

  do {
    n = recvmsg(sock, &msg, MSG_WAITALL);
  } while(n < 0 && errno == EINTR);

  if(n != sizeof(*job)) {
    return -1;
  }

  *fd = -1;
  for(cmsg=CMSG_FIRSTHDR(&msg); cmsg; cmsg=CMSG_NXTHDR(&msg, cmsg)) {
    if(cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
      memcpy(fd, CMSG_DATA(cmsg), sizeof(int));
    }
  }

  return 0;
}


static void
writer_main(int sock) {
  uint32_t magic = SHM_MAGIC;
  job_t job;
  int fd;

  signal(SIGPIPE, SIG_IGN);

  /* proves to the parent that the mapping is really shared */
  memcpy(g_shm, &magic, sizeof(magic));
  if(write_full(sock, &magic, sizeof(magic))) {
    _exit(1);
  }

  while(recv_job(sock, &job, &fd) == 0) {
    reply_t rep;

    rep.bufidx = job.bufidx;
    rep.slot = job.slot;
    rep.error = fd < 0 ? -1 : EINVAL;
    if(fd >= 0 && job.bufidx < SHM_BUFS && job.len <= BATCH_SIZE) {
      rep.error = pwrite_full(fd, g_shm + (size_t)job.bufidx * BATCH_SIZE,
                              job.len, job.off);
    }
    if(fd >= 0) {
      close(fd);
    }
    if(write_full(sock, &rep, sizeof(rep))) {
      break;
    }
  }

  _exit(0);
}


/* ---------------------------------------------------------------------- */
/* session side                                                            */

static int async_send_fd(int fd, uint32_t bufidx, uint32_t slot, off_t off,
                         size_t len);


static void
async_disable(void) {
  if(g_sock >= 0) {
    close(g_sock);
    g_sock = -1;
  }
  if(g_writer > 0) {
    kill(g_writer, SIGKILL);
    waitpid(g_writer, NULL, 0);
    g_writer = -1;
  }
  g_async = ASYNC_OFF;
}


static void
async_start(void) {
  struct pollfd pfd;
  uint32_t magic = 0;
  struct stat st;
  int sv[2];
  pid_t pid;

  if(g_async != ASYNC_UNTRIED) {
    return;
  }
  g_async = ASYNC_OFF;

  if(stat(PS5SSHD_DIR "/sync_writes", &st) == 0) {
    return;
  }

  g_shm = mmap(NULL, (size_t)SHM_BUFS * BATCH_SIZE, PROT_READ | PROT_WRITE,
               MAP_SHARED | MAP_ANON, -1, 0);
  if(g_shm == MAP_FAILED) {
    g_shm = NULL;
    log_printf("writer: mmap failed (%s), using synchronous writes",
               strerror(errno));
    return;
  }
  memset(g_shm, 0, sizeof(magic));

  if(socketpair(AF_UNIX, SOCK_STREAM, 0, sv)) {
    log_printf("writer: socketpair failed (%s), using synchronous writes",
               strerror(errno));
    return;
  }

  if((pid=fork()) == 0) {
    for(int fd=3; fd<1024; fd++) {
      if(fd != sv[1]) {
        close(fd);
      }
    }
    writer_main(sv[1]);
  }
  close(sv[1]);
  if(pid < 0) {
    close(sv[0]);
    log_printf("writer: fork failed (%s), using synchronous writes",
               strerror(errno));
    return;
  }
  g_sock = sv[0];
  g_writer = pid;

  pfd.fd = g_sock;
  pfd.events = POLLIN;
  pfd.revents = 0;
  if(poll(&pfd, 1, 3000) != 1 || read_full(g_sock, &magic, sizeof(magic)) ||
     magic != SHM_MAGIC || memcmp(g_shm, &magic, sizeof(magic))) {
    log_printf("writer: shared memory check failed, using synchronous writes");
    async_disable();
    return;
  }

  /* check that descriptors can be passed to the writer */
  {
    int nullfd = open("/dev/null", O_WRONLY);
    reply_t rep;
    int ok = 0;

    if(nullfd >= 0 && async_send_fd(nullfd, 0, 0, 0, 0) == 0) {
      pfd.revents = 0;
      if(poll(&pfd, 1, 3000) == 1 && !read_full(g_sock, &rep, sizeof(rep)) &&
         rep.error == 0) {
        ok = 1;
      }
    }
    if(nullfd >= 0) {
      close(nullfd);
    }
    if(!ok) {
      log_printf("writer: descriptor passing failed, using synchronous writes");
      async_disable();
      return;
    }
  }

  g_async = ASYNC_ON;
  log_printf("writer: started background writer process %d", (int)pid);
}


/* Process one completion from the writer (blocking). */
static int
async_collect(void) {
  reply_t rep;

  if(g_async != ASYNC_ON || read_full(g_sock, &rep, sizeof(rep)) ||
     rep.bufidx >= SHM_BUFS || rep.slot >= BATCH_SLOTS) {
    if(g_async == ASYNC_ON) {
      log_printf("writer: lost connection to writer process");
    }
    for(int i=0; i<BATCH_SLOTS; i++) {
      if(g_batches[i].inflight) {
        g_batches[i].inflight = 0;
        g_batches[i].error = EIO;
      }
    }
    memset(g_shm_busy, 0, sizeof(g_shm_busy));
    async_disable();
    return -1;
  }

  g_shm_busy[rep.bufidx] = 0;
  g_batches[rep.slot].inflight = 0;
  if(rep.error && !g_batches[rep.slot].error) {
    g_batches[rep.slot].error = rep.error;
  }

  return 0;
}


static int
async_send_fd(int fd, uint32_t bufidx, uint32_t slot, off_t off, size_t len) {
  union {
    struct cmsghdr hdr;
    char buf[CMSG_SPACE(sizeof(int))];
  } ctrl;
  struct msghdr msg;
  struct iovec iov;
  struct cmsghdr *cmsg;
  job_t job;
  ssize_t n;

  job.bufidx = bufidx;
  job.slot = slot;
  job.off = off;
  job.len = len;

  memset(&msg, 0, sizeof(msg));
  memset(&ctrl, 0, sizeof(ctrl));
  iov.iov_base = &job;
  iov.iov_len = sizeof(job);
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  msg.msg_control = ctrl.buf;
  msg.msg_controllen = sizeof(ctrl.buf);
  cmsg = CMSG_FIRSTHDR(&msg);
  cmsg->cmsg_level = SOL_SOCKET;
  cmsg->cmsg_type = SCM_RIGHTS;
  cmsg->cmsg_len = CMSG_LEN(sizeof(int));
  memcpy(CMSG_DATA(cmsg), &fd, sizeof(int));

  do {
    n = sendmsg(g_sock, &msg, 0);
  } while(n < 0 && errno == EINTR);

  return n == sizeof(job) ? 0 : -1;
}


static int
async_send(batch_t *b, uint32_t bufidx) {
  return async_send_fd(b->fd, bufidx, (uint32_t)(b - g_batches), b->start,
                       b->len);
}


/* Wait until the handle has nothing in flight. Returns the sticky error. */
static int
batch_wait(batch_t *b) {
  while(b->inflight) {
    if(async_collect()) {
      break;
    }
  }
  return b->error;
}


static int
sync_write(batch_t *b) {
  int err = pwrite_full(b->fd, b->buf, b->len, b->start);

  b->len = 0;
  if(err) {
    b->error = err;
  }
  return err;
}


/* Hand the filled block to the writer. Returns 0 or -1 with errno set. */
static int
batch_submit(batch_t *b) {
  int err = batch_wait(b);
  int bufidx = -1;

  if(err) {
    b->len = 0;
    errno = err;
    return -1;
  }
  if(!b->len) {
    return 0;
  }

  while(g_async == ASYNC_ON && bufidx < 0) {
    for(int i=0; i<SHM_BUFS; i++) {
      if(!g_shm_busy[i]) {
        bufidx = i;
        break;
      }
    }
    if(bufidx < 0) {
      async_collect();
    }
  }

  if(g_async == ASYNC_ON && bufidx >= 0) {
    memcpy(g_shm + (size_t)bufidx * BATCH_SIZE, b->buf, b->len);
    if(async_send(b, bufidx) == 0) {
      g_shm_busy[bufidx] = 1;
      b->inflight = 1;
      b->len = 0;
      return 0;
    }
    log_printf("writer: unable to pass job (%s), using synchronous writes",
               strerror(errno));
    for(int i=0; i<BATCH_SLOTS; i++) {
      batch_wait(&g_batches[i]);
    }
    async_disable();
  }

  if((err=sync_write(b))) {
    errno = err;
    return -1;
  }
  return 0;
}


/* Submit pending data and wait until it is on disk. */
static int
batch_sync(batch_t *b) {
  int err;

  if(batch_submit(b)) {
    return -1;
  }
  if((err=batch_wait(b))) {
    errno = err;
    return -1;
  }
  return 0;
}


static batch_t*
batch_find(int fd) {
  for(int i=0; i<BATCH_SLOTS; i++) {
    if(g_batches[i].used && g_batches[i].fd == fd) {
      return &g_batches[i];
    }
  }
  return NULL;
}


static batch_t*
batch_alloc(int fd) {
  async_start();

  for(int i=0; i<BATCH_SLOTS; i++) {
    batch_t *b = &g_batches[i];

    if(b->used) {
      continue;
    }
    if(!(b->buf=malloc(BATCH_SIZE))) {
      return NULL;
    }
    b->fd = fd;
    b->start = 0;
    b->len = 0;
    b->inflight = 0;
    b->error = 0;
    b->used = 1;
    return b;
  }

  return NULL;
}


static void
batch_release(batch_t *b) {
  batch_wait(b);
  free(b->buf);
  memset(b, 0, sizeof(*b));
}


static int
sync_fd(int fd) {
  batch_t *b = batch_find(fd);

  return b ? batch_sync(b) : 0;
}


int
sftpio_pwrite(int fd, unsigned char *buf, unsigned int sz,
           const unsigned int *shortOffset) {
  off_t off = (off_t)(((uint64_t)shortOffset[1] << 32) | shortOffset[0]);
  batch_t *b = batch_find(fd);

  if(b && b->len && off == b->start + (off_t)b->len) {
    size_t room = BATCH_SIZE - b->len;
    size_t n = sz < room ? sz : room;

    memcpy(b->buf + b->len, buf, n);
    b->len += n;
    if(b->len == BATCH_SIZE && batch_submit(b)) {
      return -1;
    }
    if(n < sz) {
      b->start = off + n;
      b->len = sz - n;
      memcpy(b->buf, buf + n, sz - n);
    }
    return (int)sz;
  }

  if(!b && !(b=batch_alloc(fd))) {
    return (int)pwrite(fd, buf, sz, off);
  }

  if(b->len && batch_submit(b)) {
    return -1;
  }

  if(sz >= BATCH_SIZE) {
    int err;

    if((err=batch_wait(b))) {
      errno = err;
      return -1;
    }
    return (int)pwrite(fd, buf, sz, off);
  }

  b->start = off;
  b->len = sz;
  memcpy(b->buf, buf, sz);

  return (int)sz;
}


int
sftpio_pread(int fd, unsigned char *buf, unsigned int sz,
          const unsigned int *shortOffset) {
  off_t off = (off_t)(((uint64_t)shortOffset[1] << 32) | shortOffset[0]);

  if(sync_fd(fd)) {
    return -1;
  }
  return (int)pread(fd, buf, sz, off);
}


int
sftpio_close(int fd) {
  batch_t *b = batch_find(fd);
  int ret = 0;
  int err = 0;

  if(b) {
    if(batch_sync(b)) {
      err = errno;
      ret = -1;
    }
    batch_release(b);
  }
  if(close(fd) && !ret) {
    err = errno;
    ret = -1;
  }
  if(ret) {
    errno = err;
  }

  return ret;
}


int
sftpio_fstat(int fd, struct stat *st) {
  if(sync_fd(fd)) {
    return -1;
  }
  return fstat(fd, st);
}


int
sftpio_ftruncate(int fd, off_t len) {
  if(sync_fd(fd)) {
    return -1;
  }
  return ftruncate(fd, len);
}


int
sftpio_fchmod(int fd, mode_t mode) {
  if(sync_fd(fd)) {
    return -1;
  }
  return fchmod(fd, mode);
}


int
sftpio_fchown(int fd, uid_t uid, gid_t gid) {
  if(sync_fd(fd)) {
    return -1;
  }
  return fchown(fd, uid, gid);
}


ssize_t
sftpio_write(int fd, const void *buf, size_t len) {
  if(sync_fd(fd)) {
    return -1;
  }
  return write(fd, buf, len);
}


int
sftpio_statvfs(const char *path, int fd, uint64_t out[11]) {
#ifdef PS5SSHD_HOST
  struct statvfs st;

  if(fd >= 0 ? (sync_fd(fd) || fstatvfs(fd, &st)) : statvfs(path, &st)) {
    return -1;
  }
  out[0] = st.f_bsize;
  out[1] = st.f_frsize;
  out[2] = st.f_blocks;
  out[3] = st.f_bfree;
  out[4] = st.f_bavail;
  out[5] = st.f_files;
  out[6] = st.f_ffree;
  out[7] = st.f_favail;
  out[8] = st.f_fsid;
  out[9] = st.f_flag & (ST_RDONLY | ST_NOSUID);
  out[10] = st.f_namemax;
#else
  /* statfs is a plain syscall on the PS5, statvfs is not reliably linkable */
  struct statfs st;

  if(fd >= 0 ? (sync_fd(fd) || fstatfs(fd, &st)) : statfs(path, &st)) {
    return -1;
  }
  out[0] = st.f_iosize;
  out[1] = st.f_bsize;
  out[2] = st.f_blocks;
  out[3] = st.f_bfree;
  out[4] = st.f_bavail < 0 ? 0 : (uint64_t)st.f_bavail;
  out[5] = st.f_files;
  out[6] = st.f_ffree < 0 ? 0 : (uint64_t)st.f_ffree;
  out[7] = out[6];
  out[8] = ((uint64_t)(uint32_t)st.f_fsid.val[0] << 32) |
           (uint32_t)st.f_fsid.val[1];
  out[9] = ((st.f_flags & MNT_RDONLY) ? 0x1 : 0) |
           ((st.f_flags & MNT_NOSUID) ? 0x2 : 0);
  out[10] = st.f_namemax;
#endif

  return 0;
}


void
sftpio_flush_all(void) {
  for(int i=0; i<BATCH_SLOTS; i++) {
    if(g_batches[i].used) {
      batch_sync(&g_batches[i]);
      batch_release(&g_batches[i]);
    }
  }
  if(g_sock >= 0) {
    close(g_sock);
    g_sock = -1;
  }
  if(g_writer > 0) {
    waitpid(g_writer, NULL, 0);
    g_writer = -1;
  }
}
