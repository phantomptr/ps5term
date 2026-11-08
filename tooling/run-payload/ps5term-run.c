/* PS5Term - ps5term-run: start a PS5 program for a PS5Term tab. GPL-3.0-or-later.
 *
 * PS5Term cannot create processes itself, so it sends this payload to the console's ELF
 * loader (port 9021). It reads the request PS5Term wrote, connects back over loopback twice
 * (data, control), and starts the program with the data socket as stdin, stdout and stderr
 * through shsrv's elfldr_spawn. Control carries "started <pid>" / "failed <reason>", then
 * "exited <code>" / "killed <signal>"; PS5Term sends "int" for SIGINT and closes control to
 * stop the program. See docs/superpowers/specs/2026-11-08-ps5-programs.md. */
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <ps5/klog.h>

#include "../../third_party/shsrv/elfldr.h"
#include "run_request.h"

#define REQUEST_PATH "/data/ps5term/run/request"
#define MAX_PROGRAM (256 * 1024 * 1024)

static int say(int fd, const char *format, const char *text, long number)
{
    char line[512];
    int size = text ? snprintf(line, sizeof line, format, text)
                    : snprintf(line, sizeof line, format, number);
    if (size < 0 || (size_t)size >= sizeof line)
        return -1;
    for (int done = 0; done < size;)
    {
        ssize_t wrote = write(fd, line + done, (size_t)(size - done));
        if (wrote < 0 && errno == EINTR)
            continue;
        if (wrote <= 0)
            return -1;
        done += (int)wrote;
    }
    return 0;
}

static int connect_back(unsigned port, const char *kind, const char *token)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    struct sockaddr_in address;
    memset(&address, 0, sizeof address);
    address.sin_family = AF_INET;
    address.sin_port = htons((uint16_t)port);
#if defined(PS5TERM_RUN_DEBUG_HOST)
    address.sin_addr.s_addr = htonl(PS5TERM_RUN_DEBUG_HOST); /* Development builds only. */
#else
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
#endif
    char hello[64];
    snprintf(hello, sizeof hello, "%s %s\n", kind, token);
    if (connect(fd, (struct sockaddr *)&address, sizeof address) || say(fd, "%s", hello, 0))
    {
        klog_printf("ps5term-run: %s connection to port %u failed: %s\n", kind, port,
                    strerror(errno));
        close(fd);
        return -1;
    }
    return fd;
}

static uint8_t *read_file(const char *path, size_t limit, size_t *size)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return NULL;
    struct stat st;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0 || (size_t)st.st_size > limit)
    {
        close(fd);
        errno = errno ? errno : EINVAL;
        return NULL;
    }
    uint8_t *bytes = malloc((size_t)st.st_size + 1);
    size_t done = 0;
    while (bytes && done < (size_t)st.st_size)
    {
        ssize_t got = read(fd, bytes + done, (size_t)st.st_size - done);
        if (got < 0 && errno == EINTR)
            continue;
        if (got <= 0)
        {
            free(bytes);
            bytes = NULL;
            break;
        }
        done += (size_t)got;
    }
    close(fd);
    if (bytes)
    {
        bytes[done] = 0;
        *size = done;
    }
    return bytes;
}

int main(void)
{
    size_t size = 0;
    char *text = (char *)read_file(REQUEST_PATH, ps5term_run_max_text, &size);
    if (!text)
        klog_printf("ps5term-run: cannot read %s: %s\n", REQUEST_PATH, strerror(errno));
    unlink(REQUEST_PATH); /* One request, one launch. */
    static Ps5termRunRequest request;
    if (!text)
        return 1;
    if (ps5term_run_parse(text, size, &request))
    {
        klog_printf("ps5term-run: malformed request (%zu bytes)\n", size);
        return 1;
    }
    free(text);
    int data = connect_back(request.port, "data", request.token);
    int control = data < 0 ? -1 : connect_back(request.port, "control", request.token);
    if (control < 0)
        return 1;

    size_t program_size = 0;
    uint8_t *program = read_file(request.argv[0], MAX_PROGRAM, &program_size);
    if (!program)
    {
        say(control, "failed cannot read the program: %s\n", strerror(errno), 0);
        return 1;
    }
    if (program_size < 4 || memcmp(program, "\177ELF", 4))
    {
        say(control, "failed %s\n", "not an ELF program (signed .self files do not run here)", 0);
        return 1;
    }
    /* What a terminal would tell the program; elfldr_spawn passes this environment on. */
    char number[16];
    setenv("TERM", "xterm-256color", 1);
    setenv("TERMINFO", "/data/ps5term/terminfo", 1);
    snprintf(number, sizeof number, "%u", request.columns);
    setenv("COLUMNS", number, 1);
    snprintf(number, sizeof number, "%u", request.rows);
    setenv("LINES", number, 1);
    setenv("PWD", request.cwd, 1);
    if (!getenv("HOME"))
        setenv("HOME", "/data", 1);
    if (chdir(request.cwd))
        chdir("/");

    klog_printf("ps5term-run: starting %s for PS5Term port %u\n", request.argv[0], request.port);
    pid_t pid = elfldr_spawn(data, data, data, program, request.argv);
    free(program);
    close(data); /* The program has its own copy. */
    if (pid < 0)
    {
        say(control, "failed %s\n", "the loader could not start it", 0);
        return 1;
    }
    say(control, "started %ld\n", NULL, (long)pid);

    char line[64];
    size_t line_size = 0;
    for (;;)
    {
        int status = 0;
        pid_t done = waitpid(pid, &status, WNOHANG);
        if (done == pid)
        {
            if (WIFSIGNALED(status))
                say(control, "killed %ld\n", NULL, (long)WTERMSIG(status));
            else
                say(control, "exited %ld\n", NULL, (long)WEXITSTATUS(status));
            break;
        }
        if (done < 0 && errno != EINTR)
        {
            say(control, "exited %ld\n", NULL, -1L);
            break;
        }
        struct pollfd wait = {control, POLLIN, 0};
        if (poll(&wait, 1, 100) <= 0)
            continue;
        ssize_t got = read(control, line + line_size, sizeof line - 1 - line_size);
        if (got <= 0)
        {
            /* PS5Term closed the tab or quit: the program goes with it. */
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            break;
        }
        line_size += (size_t)got;
        line[line_size] = '\0';
        for (char *end; (end = strchr(line, '\n'));)
        {
            *end = '\0';
            if (!strcmp(line, "int"))
                kill(pid, SIGINT);
            else if (!strcmp(line, "term"))
                kill(pid, SIGTERM);
            line_size -= (size_t)(end + 1 - line);
            memmove(line, end + 1, line_size + 1);
        }
        if (line_size == sizeof line - 1)
            line_size = 0; /* Nothing PS5Term sends is this long. */
    }
    close(control);
    return 0;
}
