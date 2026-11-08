/* PS5Term Linux PTY agent, GPL-3.0-or-later.
 * Guest-only experimental component. No sockets, threads or PS5 system calls.
 * Wire contract: docs/superpowers/specs/2026-11-03-linux-ssh-agent.md. */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

enum { OPEN=1, OPENED, INPUT, INPUT_OK, OUTPUT, CREDIT, RESIZE, CLOSE, EXIT, HELLO, EXEC, INPUT_EOF };
enum { CONTROL_ID=0xffffffffu }; /* HELLO uses this session ID. */
enum { CLIENTS=4, PAYLOAD=4096, HEADER=16, CREDIT_LIMIT=8192, QUEUE=65536 };
/* EXEC alone may carry a long command (SSH clients such as Warp send several KiB). */
enum { COMMAND_LIMIT=16384, EXEC_PAYLOAD=5+32+COMMAND_LIMIT };
/* In EXEC's TERM length byte: run the command on a PTY (the client asked for one). */
enum { EXEC_ON_PTY=0x80 };
typedef struct {
    uint32_t id, credit;
    pid_t pid;
    int fd, closing, reaped, eof, exec, input_eof;
    uint32_t status;
    unsigned char input[PAYLOAD];
    size_t input_size, input_at;
} Session;
static Session sessions[CLIENTS];
static unsigned char outgoing[QUEUE], incoming[HEADER+EXEC_PAYLOAD];
static size_t out_size, in_size;
static uint32_t last_id;
static volatile sig_atomic_t stopping;
static uint32_t u32(const unsigned char *p)
{ return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3]; }
static unsigned u16(const unsigned char *p) { return (unsigned)p[0]<<8 | p[1]; }
static void put32(unsigned char *p,uint32_t n)
{ p[0]=(unsigned char)(n>>24);p[1]=(unsigned char)(n>>16);p[2]=(unsigned char)(n>>8);p[3]=(unsigned char)n; }
static void on_signal(int n) { (void)n;stopping=1; }
static int nonblock(int fd)
{ int flags=fcntl(fd,F_GETFL);return flags<0 ? -1:fcntl(fd,F_SETFL,flags|O_NONBLOCK); }
static int emit(unsigned type,uint32_t id,const void *payload,size_t size)
{
    if(size>PAYLOAD || out_size+HEADER+size>QUEUE) return -1;
    unsigned char *p=outgoing+out_size;
    memcpy(p,"P5GA",4);p[4]=1;p[5]=(unsigned char)type;p[6]=p[7]=0;
    put32(p+8,id);put32(p+12,(uint32_t)size);
    if(size) memcpy(p+HEADER,payload,size);
    out_size+=HEADER+size;return 0;
}
static int number(unsigned type,uint32_t id,uint32_t value)
{ unsigned char p[4];put32(p,value);return emit(type,id,p,4); }
static Session *find(uint32_t id)
{ for(unsigned i=0;i<CLIENTS;i++) if(sessions[i].id==id) return &sessions[i];return NULL; }
/* Interactive jobs have their own process groups but retain the shell's
   session ID. /proc is mounted by guest init. This handles ordinary shell jobs;
   intentionally daemonized processes can leave the session (not containment). */
static int kill_jobs(pid_t sid)
{
    if(sid<=0) return -1;
    DIR *dir=opendir("/proc");if(!dir) return -1;
    struct dirent *entry;
    while((entry=readdir(dir))) {
        char *end;long value=strtol(entry->d_name,&end,10);
        if(*end || value<=0 || value>INT32_MAX) continue;
        pid_t pid=(pid_t)value;
        if(getsid(pid)==sid) kill(pid,SIGKILL);
    }
    closedir(dir);return 0;
}
static void terminate(Session *s)
{
    if(!s->closing) {
        s->closing=1;
        /* setsid in the child creates a group. Also kill its PID to cover the
           race before setsid. Do not target any host/application process. */
        kill(-s->pid,SIGKILL);if(!s->reaped) kill(s->pid,SIGKILL);
        (void)kill_jobs(s->pid);
        if(s->fd>=0) { close(s->fd);s->fd=-1; }
        s->input_size=s->input_at=0;s->eof=1;
    }
}
static void cleanup(void)
{
    for(unsigned i=0;i<CLIENTS;i++) if(sessions[i].id) terminate(&sessions[i]);
    for(unsigned i=0;i<CLIENTS;i++) {
        Session *s=&sessions[i];
        if(s->id && !s->reaped) while(waitpid(s->pid,NULL,0)<0 && errno==EINTR) {}
    }
    /* As subreaper, collect adopted ordinary jobs too. Do not wait for a
       deliberately daemonized guest process outside the session. */
    for(unsigned tries=0;tries<100;tries++) {
        pid_t child=waitpid(-1,NULL,WNOHANG);
        if(child>0) continue;
        if(child<0 && errno==ECHILD) break;
        usleep(1000);
    }
}
static int valid_term(const unsigned char *p,size_t size)
{
    static const char *names[]={"xterm","xterm-256color","screen","screen-256color",
                                "tmux","tmux-256color","vt100","dumb"};
    for(unsigned i=0;i<sizeof(names)/sizeof(*names);i++)
        if(strlen(names[i])==size && !memcmp(p,names[i],size)) return 1;
    return 0;
}
/* OPEN: cols, rows, TERM, on a PTY. EXEC: cols, rows, one TERM length byte, TERM,
   command, on a socket pair instead: bytes pass unchanged and input can end. */
static int open_session(uint32_t id,const unsigned char *p,size_t size,int in_fd,int out_fd,const char *command,int pty)
{
    if(id==CONTROL_ID || id<=last_id || size<5 || size>4+32 || !valid_term(p+4,size-4) ||
       !u16(p) || u16(p)>500 || !u16(p+2) || u16(p+2)>200) return -1;
    Session *s=NULL;
    for(unsigned i=0;i<CLIENTS;i++) if(!sessions[i].id) { s=&sessions[i];break; }
    if(!s) return -1;
    int master=-1,peer=-1;
    char slave[128];
    if(command && !pty) {
        int pair[2];
        if(socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,pair)<0) return -1;
        master=pair[0];peer=pair[1];
        if(nonblock(master)<0) { close(master);close(peer);return -1; }
    } else {
        master=posix_openpt(O_RDWR|O_NOCTTY|O_CLOEXEC|O_NONBLOCK);
        if(master<0) return -1;
        if(grantpt(master) || unlockpt(master) || ptsname_r(master,slave,sizeof(slave)))
        { close(master);return -1; }
        struct winsize window={.ws_row=(unsigned short)u16(p+2),.ws_col=(unsigned short)u16(p)};
        if(ioctl(master,TIOCSWINSZ,&window)<0) { close(master);return -1; }
    }
    char term[38]="TERM=";memcpy(term+5,p+4,size-4);term[size+1]=0;
    pid_t pid=fork();
    if(pid<0) { close(master);if(peer>=0) close(peer);return -1; }
    if(!pid) {
        if(setsid()<0) _exit(126);
        int child=peer;
        if(!command || pty) {
            child=open(slave,O_RDWR);
            if(child<0 || ioctl(child,TIOCSCTTY,0)<0) _exit(126);
        }
        if(dup2(child,0)<0 || dup2(child,1)<0 || dup2(child,2)<0) _exit(126);
        if(child>2) close(child);
        close(master);
        for(unsigned i=0;i<CLIENTS;i++) if(sessions[i].id && sessions[i].fd>2) close(sessions[i].fd);
        if(in_fd>2) close(in_fd);
        if(out_fd>2 && out_fd!=in_fd) close(out_fd);
        signal(SIGPIPE,SIG_DFL);signal(SIGTERM,SIG_DFL);signal(SIGINT,SIG_DFL);
        signal(SIGHUP,SIG_DFL);signal(SIGQUIT,SIG_DFL);
        if(chdir("/root")<0 && chdir("/")<0) _exit(126);
        // A command gets no login profile, so what it needs from one is set here.
        char *env[]={term,"PATH=/mnt/data/bin:/usr/bin:/bin:/usr/sbin:/sbin","HOME=/root",
                     "SHELL=/bin/sh","LANG=C.UTF-8","USER=root",
                     "SSL_CERT_FILE=/etc/ssl/certs/ca-certificates.crt",NULL};
        char *login[]={"/bin/sh","-l",NULL};
        char *run[]={"/bin/sh","-c",(char *)command,NULL};
        execve("/bin/sh",command ? run:login,env);_exit(127);
    }
    if(peer>=0) close(peer);
    *s=(Session){.id=id,.pid=pid,.fd=master,.exec=command!=NULL && !pty};last_id=id;
    return number(OPENED,id,PAYLOAD);
}
static int handle_frame(int in_fd,int out_fd)
{
    unsigned type=incoming[5];uint32_t id=u32(incoming+8),size=u32(incoming+12);
    const unsigned char *p=incoming+HEADER;
    if(type==OPEN) return open_session(id,p,size,in_fd,out_fd,NULL,1);
    if(type==EXEC) {
        /* cols, rows, TERM length (EXEC_ON_PTY flag), TERM, command (COMMAND_LIMIT). */
        if(size<6) return -1;
        const int pty=(p[4]&EXEC_ON_PTY)!=0;
        const size_t term_size=p[4]&~EXEC_ON_PTY&0xff;
        if(!term_size || term_size>32 || size<=5+term_size) return -1;
        static char command[COMMAND_LIMIT+1];
        const size_t command_size=size-5-term_size;
        if(command_size>COMMAND_LIMIT) return -1;
        memcpy(command,p+5+term_size,command_size);command[command_size]=0;
        if(strlen(command)!=command_size) return -1;
        unsigned char opening[4+32];
        memcpy(opening,p,4);memcpy(opening+4,p+5,term_size);
        return open_session(id,opening,4+term_size,in_fd,out_fd,command,pty);
    }
    /* Validate even retired traffic. Late input/credit/close can legitimately
       cross EXIT in flight; IDs never recycle, so discarding it cannot deliver
       bytes to a different shell. Future IDs remain protocol errors. */
    if(id==CONTROL_ID) return -1;
    int valid=type==INPUT ? size>0:
        type==CREDIT ? size==4 && u32(p)>0 && u32(p)<=CREDIT_LIMIT:
        type==RESIZE ? size==4 && u16(p)>0 && u16(p)<=500 && u16(p+2)>0 && u16(p+2)<=200:
        type==CLOSE || type==INPUT_EOF ? size==0:0;
    if(!valid) return -1;
    Session *s=find(id);
    if(!s || s->closing) return id<=last_id ? 0:-1;
    switch(type) {
    case INPUT:
        if(!size || s->input_size) return -1;
        memcpy(s->input,p,size);s->input_size=size;s->input_at=0;return 0;
    case CREDIT:
        if(size!=4 || !u32(p) || u32(p)>CREDIT_LIMIT-s->credit) return -1;
        s->credit+=u32(p);return 0;
    case RESIZE: {
        if(size!=4 || !u16(p) || u16(p)>500 || !u16(p+2) || u16(p+2)>200) return -1;
        if(s->exec) return 0;
        struct winsize window={.ws_row=(unsigned short)u16(p+2),.ws_col=(unsigned short)u16(p)};
        return ioctl(s->fd,TIOCSWINSZ,&window)<0 ? -1:0;
    }
    case INPUT_EOF:
        /* Applied once queued input has been written. A PTY has no such state. */
        s->input_eof=1;return 0;
    case CLOSE:
        if(size) return -1;
        terminate(s);return 0;
    default:return -1;
    }
}
static int read_frame(int fd,int out_fd)
{
    size_t target=HEADER;
    if(in_size>=HEADER) target+=u32(incoming+12);
    ssize_t n=read(fd,incoming+in_size,target-in_size);
    if(!n) return in_size ? -1:1;
    if(n<0) return errno==EINTR || errno==EAGAIN || errno==EWOULDBLOCK ? 0:-1;
    in_size+=(size_t)n;
    if(in_size>=HEADER) {
        if(memcmp(incoming,"P5GA",4) || incoming[4]!=1 || incoming[6] || incoming[7] ||
           !u32(incoming+8) || u32(incoming+12)>(incoming[5]==EXEC ? EXEC_PAYLOAD:PAYLOAD)) return -1;
        target=HEADER+u32(incoming+12);
        if(in_size==target) {
            if(handle_frame(fd,out_fd)<0) return -1;
            in_size=0;
        }
    }
    return 0;
}
static int pump_session(Session *s)
{
    if(s->fd>=0 && s->input_size && out_size+HEADER+4<=QUEUE) {
        ssize_t n=write(s->fd,s->input+s->input_at,s->input_size-s->input_at);
        if(n>0) {
            s->input_at+=(size_t)n;
            if(s->input_at==s->input_size) {
                if(number(INPUT_OK,s->id,(uint32_t)s->input_size)<0) return -1;
                s->input_size=s->input_at=0;
            }
        } else if(n<0 && errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR && errno!=EIO &&
                  errno!=EPIPE && errno!=ECONNRESET) return -1;
        else if(n<0 && (errno==EPIPE || errno==ECONNRESET)) {
            /* The command stopped reading: discard the input, but acknowledge it so
               the host can send more (or end) instead of waiting forever. */
            if(number(INPUT_OK,s->id,(uint32_t)s->input_size)<0) return -1;
            s->input_size=s->input_at=0;
        }
    }
    if(s->fd>=0 && s->input_eof && !s->input_size) {
        if(s->exec) (void)shutdown(s->fd,SHUT_WR);
        s->input_eof=0;
    }
    if(s->fd>=0 && s->credit && !s->eof && out_size+HEADER+PAYLOAD<=QUEUE) {
        unsigned char data[PAYLOAD];size_t count=s->credit<PAYLOAD ? s->credit:PAYLOAD;
        ssize_t n=read(s->fd,data,count);
        if(n>0) {
            if(emit(OUTPUT,s->id,data,(size_t)n)<0) return -1;
            s->credit-=(uint32_t)n;
        } else if(!n || (n<0 && errno==EIO && s->reaped) || (n<0 && errno==ECONNRESET)) {
            /* ECONNRESET: an exec command exited with input it never read. That ends
               this session's output; it is not an agent failure. */
            s->eof=1;
        }
        else if(n<0 && errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR && errno!=EIO) return -1;
    }
    if(s->reaped && s->eof && out_size+HEADER+4<=QUEUE) {
        if(number(EXIT,s->id,s->status)<0) return -1;
        if(s->fd>=0) close(s->fd);
        memset(s,0,sizeof(*s));s->fd=-1;
    }
    return 0;
}
static int reap_children(void)
{
    for(;;) {
        int status;pid_t pid=waitpid(-1,&status,WNOHANG);
        if(pid<=0) return pid<0 && errno!=ECHILD && errno!=EINTR ? -1:0;
        for(unsigned i=0;i<CLIENTS;i++) {
            Session *s=&sessions[i];
            if(s->id && s->pid==pid) {
                s->reaped=1;
                s->status=WIFEXITED(status) ? (uint32_t)WEXITSTATUS(status):128u+(unsigned)WTERMSIG(status);
                if(kill_jobs(s->pid)<0) return -1;
                break;
            }
        }
    }
}
int main(int argc,char **argv)
{
    int in_fd=0,out_fd=1,result=0;
    if(argc==3 && !strcmp(argv[1],"--device")) {
        in_fd=open(argv[2],O_RDWR|O_NOCTTY|O_CLOEXEC|O_NONBLOCK);
        if(in_fd<0) return 2;
        out_fd=in_fd;
        struct termios tty;
        if(tcgetattr(in_fd,&tty)<0) { close(in_fd);return 2; }
        cfmakeraw(&tty);
        if(tcsetattr(in_fd,TCSANOW,&tty)<0) { close(in_fd);return 2; }
    } else if(argc!=1) return 2;
    if(nonblock(in_fd)<0 || nonblock(out_fd)<0) return 2;
    /* This runs only inside Linux, never in the native PS5 app. */
    if(prctl(PR_SET_CHILD_SUBREAPER,1)<0) return 2;
    DIR *proc=opendir("/proc");if(!proc) return 2;closedir(proc);
    signal(SIGPIPE,SIG_IGN);signal(SIGTERM,on_signal);signal(SIGINT,on_signal);signal(SIGHUP,on_signal);
    /* The device echoes until now; the host sends nothing before this. */
    if(emit(HELLO,CONTROL_ID,NULL,0)<0) return 2;
    while(!stopping) {
        if(reap_children()<0) { result=2;break; }
        /* At most one incoming frame plus one PTY read per session per pass.
           Keep room for OPENED/control frames even with a non-reading host. */
        struct pollfd fds[2+CLIENTS]={{in_fd,out_size+HEADER+PAYLOAD<=QUEUE ? POLLIN:0,0},
                                    {out_fd,out_size ? POLLOUT:0,0}};
        for(unsigned i=0;i<CLIENTS;i++) {
            Session *s=&sessions[i];short events=0;
            if(s->id && s->fd>=0) {
                if(s->credit && !s->eof && out_size+HEADER+PAYLOAD<=QUEUE) events|=POLLIN;
                if(s->input_size && out_size+HEADER+4<=QUEUE) events|=POLLOUT;
            }
            /* Do not poll a stalled fd: POLLHUP can otherwise spin forever. */
            fds[2+i]=(struct pollfd){events ? s->fd:-1,events,0};
        }
        int n=poll(fds,2+CLIENTS,20);
        if(n<0 && errno!=EINTR) { result=2;break; }
        if(fds[0].revents&(POLLIN|POLLHUP|POLLERR|POLLNVAL)) {
            int read_result=read_frame(in_fd,out_fd);
            if(read_result) { result=read_result<0 ? 2:0;break; }
        }
        if(out_size && (fds[1].revents&POLLOUT)) {
            ssize_t written=write(out_fd,outgoing,out_size);
            if(written>0) { out_size-=(size_t)written;memmove(outgoing,outgoing+written,out_size); }
            else if(written<0 && errno!=EINTR && errno!=EAGAIN && errno!=EWOULDBLOCK) { result=2;break; }
        }
        if(fds[1].revents&(POLLERR|POLLHUP|POLLNVAL)) { result=2;break; }
        for(unsigned i=0;i<CLIENTS;i++) if(sessions[i].id && pump_session(&sessions[i])<0) { result=2;stopping=1;break; }
    }
    cleanup();
    if(in_fd>2) close(in_fd);
    return result;
}
