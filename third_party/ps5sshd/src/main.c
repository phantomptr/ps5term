#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/sysctl.h>
#include <sys/wait.h>

#ifndef PS5SSHD_HOST
#include <ps5/kernel.h>
#endif

#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssh/ssh.h>

#include "ps5sshd.h"


#define PROC_NAME "ps5sshd.elf"


#ifndef PS5SSHD_HOST
static pid_t
find_pid(const char* name) {
  int mib[4] = {1, 14, 8, 0};
  pid_t mypid = getpid();
  pid_t pid = -1;
  size_t buf_size;
  uint8_t *buf;

  if(sysctl(mib, 4, 0, &buf_size, 0, 0)) {
    return -1;
  }
  if(!(buf=malloc(buf_size))) {
    return -1;
  }
  if(sysctl(mib, 4, buf, &buf_size, 0, 0)) {
    free(buf);
    return -1;
  }

  for(uint8_t *ptr=buf; ptr<(buf+buf_size);) {
    int ki_structsize = *(int*)ptr;
    pid_t ki_pid = *(pid_t*)&ptr[72];
    char *ki_tdname = (char*)&ptr[447];

    if(ki_structsize <= 0) {
      break;
    }
    ptr += ki_structsize;
    if(!strcmp(name, ki_tdname) && mypid != ki_pid) {
      pid = ki_pid;
    }
  }

  free(buf);

  return pid;
}


static void
init_stdio(void) {
  int fd = open("/dev/console", O_RDWR);

  if(fd < 0) {
    return;
  }
  dup2(fd, STDIN_FILENO);
  dup2(fd, STDOUT_FILENO);
  dup2(fd, STDERR_FILENO);
  if(fd > STDERR_FILENO) {
    close(fd);
  }
}


static void
elevate_privileges(void) {
  uint8_t caps[16];
  pid_t pid = getpid();

  memset(caps, 0xff, sizeof(caps));
  kernel_set_ucred_uid(pid, 0);
  kernel_set_ucred_ruid(pid, 0);
  kernel_set_ucred_svuid(pid, 0);
  kernel_set_ucred_rgid(pid, 0);
  kernel_set_ucred_svgid(pid, 0);
  kernel_set_ucred_caps(pid, caps);
  kernel_set_ucred_authid(pid, 0x4801000000000013l);
}
#endif


#define MAX_SESSIONS      16
#define MAX_TRACKED_HOSTS 32
#define BAN_THRESHOLD     5
#define BAN_WINDOW_SECS   (10 * 60)
#define BAN_SECS          (10 * 60)


typedef struct session_slot {
  pid_t          pid;
  struct in_addr addr;
} session_slot_t;


typedef struct host_record {
  struct in_addr addr;
  int            failures;
  time_t         first_failure;
  time_t         banned_until;
} host_record_t;


static session_slot_t g_sessions[MAX_SESSIONS];
static host_record_t  g_hosts[MAX_TRACKED_HOSTS];


static host_record_t*
host_lookup(struct in_addr addr, int create) {
  host_record_t *oldest = &g_hosts[0];
  time_t now = time(NULL);

  for(int i=0; i<MAX_TRACKED_HOSTS; i++) {
    host_record_t *h = &g_hosts[i];
    if(h->failures && h->addr.s_addr == addr.s_addr) {
      return h;
    }
  }
  if(!create) {
    return NULL;
  }
  for(int i=0; i<MAX_TRACKED_HOSTS; i++) {
    host_record_t *h = &g_hosts[i];
    if(!h->failures || (h->banned_until < now &&
                        now - h->first_failure > BAN_WINDOW_SECS)) {
      oldest = h;
      break;
    }
    if(h->first_failure < oldest->first_failure) {
      oldest = h;
    }
  }
  memset(oldest, 0, sizeof(*oldest));
  oldest->addr = addr;

  return oldest;
}


static void
host_record_failure(struct in_addr addr) {
  host_record_t *h = host_lookup(addr, 1);
  time_t now = time(NULL);
  char ip[INET_ADDRSTRLEN];

  if(!h->failures || now - h->first_failure > BAN_WINDOW_SECS) {
    h->failures = 0;
    h->first_failure = now;
  }
  h->failures++;
  if(h->failures >= BAN_THRESHOLD) {
    h->banned_until = now + BAN_SECS;
    inet_ntop(AF_INET, &addr, ip, sizeof(ip));
    log_printf("too many failed logins from %s, blocking it for %d minutes",
               ip, BAN_SECS / 60);
  }
}


static int
host_is_banned(struct in_addr addr) {
  host_record_t *h = host_lookup(addr, 0);

  return h && h->banned_until > time(NULL);
}


static void
reap_sessions(void) {
  int status;
  pid_t pid;

  while((pid=waitpid(-1, &status, WNOHANG)) > 0) {
    for(int i=0; i<MAX_SESSIONS; i++) {
      if(g_sessions[i].pid != pid) {
        continue;
      }
      if(WIFEXITED(status) && WEXITSTATUS(status) == SESSION_AUTH_FAILED) {
        host_record_failure(g_sessions[i].addr);
      }
      g_sessions[i].pid = 0;
      break;
    }
  }
}


static session_slot_t*
free_session_slot(void) {
  for(int i=0; i<MAX_SESSIONS; i++) {
    if(!g_sessions[i].pid) {
      return &g_sessions[i];
    }
  }
  return NULL;
}


/* Returns the number of addresses found (0 while the network is down). */
static int
announce(int port, int notify_user) {
  char ip[INET_ADDRSTRLEN];
  struct ifaddrs *ifaddr;
  int count = 0;

  if(getifaddrs(&ifaddr) == -1) {
    return 0;
  }

  for(struct ifaddrs *ifa=ifaddr; ifa; ifa=ifa->ifa_next) {
    if(!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) {
      continue;
    }
    if(!strncmp("lo", ifa->ifa_name, 2)) {
      continue;
    }
    inet_ntop(AF_INET, &((struct sockaddr_in*)ifa->ifa_addr)->sin_addr, ip,
              sizeof(ip));
    if(!strncmp("0.", ip, 2)) {
      continue;
    }
    count++;
    if(port) {
      log_printf("listening on %s:%d (%s)", ip, port, ifa->ifa_name);
    }
    if(port && notify_user) {
      notify("ps5sshd " PS5SSHD_VERSION " listening on %s:%d", ip, port);
    }
  }

  freeifaddrs(ifaddr);

  return count;
}


static int
listen_on(int port) {
  struct sockaddr_in addr;
  int srvfd;

  if((srvfd=socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    log_printf("socket: %s", strerror(errno));
    return -1;
  }
  setsockopt(srvfd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int));

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(port);

  if(bind(srvfd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
    log_printf("bind port %d: %s", port, strerror(errno));
    close(srvfd);
    return -1;
  }
  if(listen(srvfd, 8) != 0) {
    log_printf("listen port %d: %s", port, strerror(errno));
    close(srvfd);
    return -1;
  }

  return srvfd;
}


static int
serve(int notify_user) {
  int port = PS5SSHD_PORT;
  int srvfd;

  if(!announce(0, 0)) {
    return 0;
  }

  if((srvfd=listen_on(port)) < 0) {
    port = PS5SSHD_PORT_FALLBACK;
    if((srvfd=listen_on(port)) < 0) {
      return -1;
    }
  }
  announce(port, notify_user);

  while(1) {
    struct sockaddr_in peer;
    socklen_t peer_len = sizeof(peer);
    char peer_str[INET_ADDRSTRLEN + 8];
    char ip[INET_ADDRSTRLEN];
    session_slot_t *slot;
    pid_t pid;
    int fd;

    if((fd=accept(srvfd, (struct sockaddr*)&peer, &peer_len)) < 0) {
      if(errno == EINTR) {
        continue;
      }
      log_printf("accept: %s", strerror(errno));
      break;
    }

    reap_sessions();

    inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip));
    snprintf(peer_str, sizeof(peer_str), "%s:%d", ip, ntohs(peer.sin_port));

    if(host_is_banned(peer.sin_addr)) {
      close(fd);
      continue;
    }
    if(!(slot=free_session_slot())) {
      log_printf("too many sessions, rejecting %s", peer_str);
      close(fd);
      continue;
    }
    log_printf("connection from %s", peer_str);

    if((pid=fork()) == 0) {
      close(srvfd);
      _exit(session_run(fd, peer_str));
    }
    if(pid < 0) {
      log_printf("fork: %s", strerror(errno));
    } else {
      slot->pid = pid;
      slot->addr = peer.sin_addr;
    }
    close(fd);
  }

  close(srvfd);
  return -1;
}


int
main(void) {
  int notify_user = 1;
  pid_t pid;

#ifndef PS5SSHD_HOST
  syscall(SYS_thr_set_name, -1, PROC_NAME);
  init_stdio();
  elevate_privileges();

  while((pid=find_pid(PROC_NAME)) > 0) {
    if(kill(pid, SIGKILL)) {
      break;
    }
    sleep(1);
  }
#else
  (void)pid;
#endif

  if(config_init_dir()) {
    notify("ps5sshd: unable to create " PS5SSHD_DIR);
    return 1;
  }
  log_open();
  log_printf("ps5sshd " PS5SSHD_VERSION " starting, compiled %s %s",
             __DATE__, __TIME__);

  if(config_ensure_hostkeys()) {
    notify("ps5sshd: unable to create host keys");
    return 1;
  }
  config_ensure_credentials();

  signal(SIGPIPE, SIG_IGN);

  while(1) {
    serve(notify_user);
    notify_user = 0;
    sleep(3);
  }

  return 0;
}
