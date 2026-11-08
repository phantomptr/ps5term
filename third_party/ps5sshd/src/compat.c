#include <fcntl.h>
#include <sys/socket.h>

/* wolfCrypt references accept4(), which the PS5 libraries do not export. */
int
accept4(int s, struct sockaddr *addr, socklen_t *addrlen, int flags) {
  int fd = accept(s, addr, addrlen);

  if(fd >= 0 && (flags & SOCK_CLOEXEC)) {
    fcntl(fd, F_SETFD, FD_CLOEXEC);
  }
  if(fd >= 0 && (flags & SOCK_NONBLOCK)) {
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
  }

  return fd;
}
