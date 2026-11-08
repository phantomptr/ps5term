/* Copyright (C) 2024 John Törnblom

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 3, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING. If not, see
<http://www.gnu.org/licenses/>.  */

/* Shell used by ps5sshd sessions. Derived from shsrv's sh.c with the telnet
   transport removed: stdin/stdout/stderr are plain pipes owned by the SSH
   server, and "sh -c CMD" runs a single command line for exec requests. */

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/wait.h>

#include "builtin.h"
#include "elfldr.h"


#define SHELL_LINE_BUFSIZE 1024
#define SHELL_TOK_BUFSIZE  128


typedef struct sce_version {
  unsigned long unknown1;
  char          str_version[0x1c];
  unsigned int  bin_version;
  unsigned long unknown2;
} sce_version_t;


int  sceKernelGetProsperoSystemSwVersion(sce_version_t *);
int  sceKernelGetHwModelName(char *);
long sceKernelGetCpuFrequency(void);
int  sceKernelGetCpuTemperature(int *);
int  sceKernelGetSocSensorTemperature(int, int *);


int sh_chmod(int argc, char **argv);
int sh_rm(int argc, char **argv);


static volatile pid_t g_fg_pid = -1;
volatile sig_atomic_t g_interrupted = 0;
static int g_last_status = -1;


/**
 * The SSH server sends SIGUSR1 when the user presses Ctrl-C. SIGINT is not
 * used because Prospero stops processes on SIGINT instead of terminating them.
 **/
static void
sh_on_interrupt(int sig) {
  pid_t pid = g_fg_pid;

  (void)sig;
  if(pid > 0) {
    kill(pid, SIGKILL);
  } else {
    g_interrupted = 1;
  }
}


static char*
sh_readline(void) {
  int bufsize = SHELL_LINE_BUFSIZE;
  int position = 0;
  char *buffer_backup;
  char *buffer = calloc(bufsize, sizeof(char));
  char c;

  if(!buffer) {
    perror("calloc");
    return NULL;
  }

  while(1) {
    ssize_t len = read(STDIN_FILENO, &c, 1);
    if(len == -1 && errno == EINTR) {
      if(g_interrupted) {
        g_interrupted = 0;
        buffer[0] = '\0';
        return buffer;
      }
      continue;
    }

    if(len <= 0) {
      if(position > 0) {
        buffer[position] = '\0';
        return buffer;
      }
      free(buffer);
      return NULL;
    }

    if(c == '\n') {
      buffer[position] = '\0';
      return buffer;
    }

    buffer[position++] = c;

    if(position >= bufsize) {
      bufsize += SHELL_LINE_BUFSIZE;
      buffer_backup = buffer;
      buffer = realloc(buffer, bufsize);
      if(!buffer) {
        perror("realloc");
        free(buffer_backup);
        return NULL;
      }
    }
  }
}


static char**
sh_splitstring(char *line, char *delim) {
  int bufsize = SHELL_TOK_BUFSIZE;
  int position = 0;
  char **tokens = calloc(bufsize, sizeof(char*));
  char *token, **tokens_backup;
  char *state = 0;

  if(!tokens) {
    perror("calloc");
    return NULL;
  }

  token = strtok_r(line, delim, &state);
  while(token != NULL) {
    tokens[position] = token;
    position++;

    if(position >= bufsize) {
      bufsize += SHELL_TOK_BUFSIZE;
      tokens_backup = tokens;
      tokens = realloc(tokens, bufsize * sizeof(char*));
      if(!tokens) {
        perror("realloc");
        free(tokens_backup);
        return NULL;
      }
    }

    token = strtok_r(NULL, delim, &state);
  }

  tokens[position] = NULL;
  return tokens;
}


static int
sh_waitpid(pid_t pid) {
  int status;

  while(waitpid(pid, &status, WUNTRACED) < 0) {
    if(errno != EINTR) {
      perror("waitpid");
      return -1;
    }
  }

  if(WIFEXITED(status)) {
    return WEXITSTATUS(status);
  }

  if(WIFSIGNALED(status)) {
    puts(strsignal(WTERMSIG(status)));
    return 128 + WTERMSIG(status);
  }

  if(WIFSTOPPED(status)) {
    puts(strsignal(WSTOPSIG(status)));
  }

  return -1;
}


static int
sh_which(const char* name, char* path) {
  char **paths = NULL;
  char* PATH;

  if(strchr(name, '/')) {
    if(!access(name, R_OK | X_OK)) {
      strncpy(path, name, PATH_MAX - 1);
      path[PATH_MAX - 1] = '\0';
      return 0;
    }
    return -1;
  }

  if(!getenv("PATH") || !(PATH=strdup(getenv("PATH")))) {
    return -1;
  }
  if(!(paths=sh_splitstring(PATH, ":"))) {
    free(PATH);
    return -1;
  }

  for(int i=0; paths[i]; i++) {
    snprintf(path, PATH_MAX, "%s/%s", paths[i], name);
    if(!access(path, R_OK | X_OK)) {
      free(paths);
      free(PATH);
      return 0;
    }
  }

  free(paths);
  free(PATH);

  return -1;
}


static uint8_t*
sh_readfile(const char* path) {
  uint8_t* buf;
  long len;
  FILE* file;

  if(!(file=fopen(path, "rb"))) {
    perror("fopen");
    return 0;
  }

  if(fseek(file, 0, SEEK_END) || (len=ftell(file)) < 0 ||
     fseek(file, 0, SEEK_SET)) {
    perror("fseek");
    fclose(file);
    return 0;
  }

  if(!(buf=malloc(len))) {
    fclose(file);
    return 0;
  }

  if(fread(buf, 1, len, file) != (size_t)len) {
    perror("fread");
    free(buf);
    fclose(file);
    return 0;
  }

  fclose(file);
  return buf;
}


/**
 * Shell-internal echo: arguments separated by single spaces, -n suppresses
 * the trailing newline.
 **/
static int
sh_echo(int argc, char **argv) {
  int newline = 1;
  int i = 1;

  if(argc > 1 && !strcmp(argv[1], "-n")) {
    newline = 0;
    i++;
  }
  for(int first=i; i<argc; i++) {
    if(i > first) {
      fputc(' ', stdout);
    }
    fputs(argv[i], stdout);
  }
  if(newline) {
    fputc('\n', stdout);
  }
  fflush(stdout);

  return 0;
}


static pid_t
sh_execute(char **argv) {
  char path[PATH_MAX];
  pid_t pid = 0;
  uint8_t* elf;
  int argc = 0;

  while(argv[argc]) {
    argc++;
  }

  g_last_status = -1;
  if(!argc) {
    return -1;
  }

  if(!strcmp(argv[0], "echo")) {
    g_last_status = sh_echo(argc, argv);
    return 0;
  }
  if(!strcmp(argv[0], "chmod")) {
    g_last_status = sh_chmod(argc, argv);
    return 0;
  }
  if(!strcmp(argv[0], "rm")) {
    g_last_status = sh_rm(argc, argv);
    return 0;
  }

  if((pid=builtin_cmd_run(argv[0], argc, argv)) >= 0) {
    return pid;
  }

  if(!sh_which(argv[0], path) && (elf=sh_readfile(path))) {
    pid = elfldr_spawn(STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO, elf, argv);
    free(elf);
    return pid;
  }

  fprintf(stderr, "%s: command not found\n", argv[0]);
  g_last_status = 127;

  return -1;
}


enum {
  TOK_WORD,
  TOK_PIPE,
  TOK_SEQ,
  TOK_OUT,
  TOK_APPEND,
  TOK_IN,
  TOK_END
};


typedef struct sh_token {
  int   type;
  char *word;
} sh_token_t;


static void
sh_free_tokens(sh_token_t *toks) {
  if(!toks) {
    return;
  }
  for(int i=0; toks[i].type != TOK_END; i++) {
    free(toks[i].word);
  }
  free(toks);
}


static int
sh_push_token(sh_token_t **toks, int *n, int *cap, int type, char *word) {
  if(*n + 1 >= *cap) {
    int newcap = *cap ? *cap * 2 : 16;
    sh_token_t *p = realloc(*toks, newcap * sizeof(sh_token_t));
    if(!p) {
      return -1;
    }
    *toks = p;
    *cap = newcap;
  }
  (*toks)[*n].type = type;
  (*toks)[*n].word = word;
  (*n)++;
  (*toks)[*n].type = TOK_END;
  (*toks)[*n].word = NULL;

  return 0;
}


/**
 * Split a command line into words and operators. Supports '...' and "..."
 * quoting, backslash escapes and the operators | ; & > >> <.
 **/
static sh_token_t*
sh_tokenize(const char *line) {
  sh_token_t *toks = NULL;
  int n = 0, cap = 0;
  const char *p = line;

  if(sh_push_token(&toks, &n, &cap, TOK_END, NULL)) {
    return NULL;
  }
  n = 0;

  while(*p) {
    size_t len = 0, wcap = 64;
    char *word;

    if(*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
      p++;
      continue;
    }
    if(*p == '|') {
      sh_push_token(&toks, &n, &cap, TOK_PIPE, NULL);
      p++;
      continue;
    }
    if(*p == ';' || *p == '&') {
      sh_push_token(&toks, &n, &cap, TOK_SEQ, NULL);
      p++;
      continue;
    }
    if(*p == '>') {
      if(p[1] == '>') {
        sh_push_token(&toks, &n, &cap, TOK_APPEND, NULL);
        p += 2;
      } else {
        sh_push_token(&toks, &n, &cap, TOK_OUT, NULL);
        p++;
      }
      continue;
    }
    if(*p == '<') {
      sh_push_token(&toks, &n, &cap, TOK_IN, NULL);
      p++;
      continue;
    }

    if(!(word=malloc(wcap))) {
      sh_free_tokens(toks);
      return NULL;
    }

    while(*p && !strchr(" \t\r\n|;&<>", *p)) {
      char quote = 0;

      if(*p == '\'' || *p == '"') {
        quote = *p++;
      }

      do {
        char c;

        if(quote && *p == quote) {
          p++;
          break;
        }
        if(quote && !*p) {
          fprintf(stderr, "sh: unterminated quote\n");
          free(word);
          sh_free_tokens(toks);
          return NULL;
        }
        if(*p == '\\' && p[1] && (quote != '\'') &&
           (!quote || strchr("\"\\$`", p[1]))) {
          p++;
        }
        c = *p++;

        if(len + 2 > wcap) {
          char *tmp = realloc(word, wcap *= 2);
          if(!tmp) {
            free(word);
            sh_free_tokens(toks);
            return NULL;
          }
          word = tmp;
        }
        word[len++] = c;
      } while(quote);
    }

    word[len] = '\0';
    if(sh_push_token(&toks, &n, &cap, TOK_WORD, word)) {
      free(word);
      sh_free_tokens(toks);
      return NULL;
    }
  }

  return toks;
}


static int
sh_redirect(const char *path, int flags, int target) {
  int fd = open(path, flags, 0644);

  if(fd < 0) {
    fprintf(stderr, "sh: %s: %s\n", path, strerror(errno));
    return -1;
  }
  dup2(fd, target);
  close(fd);

  return 0;
}


/**
 * Run one command line. Commands are separated by ; or & (run in sequence)
 * or | (output of one becomes input of the next). Returns the exit status of
 * the last command.
 **/
static int
sh_run_line(char *line) {
  sh_token_t *toks;
  int pipe_read = -1;
  int status = 0;
  int infd, outfd;
  int i = 0;

  if(!(toks=sh_tokenize(line))) {
    return 2;
  }

  infd = dup(0);
  outfd = dup(1);

  while(toks[i].type != TOK_END) {
    char **argv = calloc(SHELL_TOK_BUFSIZE + 1, sizeof(char*));
    const char *in = NULL, *out = NULL;
    int append = 0, argc = 0, error = 0;
    int pipefd[2];
    int sep;
    pid_t pid;

    if(!argv) {
      status = 1;
      break;
    }

    while(toks[i].type != TOK_END && toks[i].type != TOK_PIPE &&
          toks[i].type != TOK_SEQ) {
      int type = toks[i].type;

      if(type == TOK_WORD) {
        if(argc < SHELL_TOK_BUFSIZE) {
          argv[argc++] = toks[i].word;
        }
        i++;
        continue;
      }
      if(toks[i+1].type != TOK_WORD) {
        fprintf(stderr, "sh: syntax error near redirection\n");
        error = 1;
        i++;
        continue;
      }
      if(type == TOK_IN) {
        in = toks[i+1].word;
      } else {
        out = toks[i+1].word;
        append = (type == TOK_APPEND);
      }
      i += 2;
    }

    sep = toks[i].type;
    if(sep != TOK_END) {
      i++;
    }

    if(pipe_read >= 0) {
      dup2(pipe_read, 0);
      close(pipe_read);
      pipe_read = -1;
    } else {
      dup2(infd, 0);
    }

    if(sep == TOK_PIPE && !pipe(pipefd)) {
      dup2(pipefd[1], 1);
      close(pipefd[1]);
      pipe_read = pipefd[0];
    } else {
      dup2(outfd, 1);
    }

    if(in && sh_redirect(in, O_RDONLY, 0)) {
      error = 1;
    }
    if(out && sh_redirect(out, O_WRONLY | O_CREAT |
                          (append ? O_APPEND : O_TRUNC), 1)) {
      error = 1;
    }

    if(error) {
      status = 1;
    } else if(argc) {
      pid = sh_execute(argv);
      if(pid > 0) {
        g_fg_pid = pid;
        status = sh_waitpid(pid);
        g_fg_pid = -1;
      } else if(pid < 0) {
        status = g_last_status >= 0 ? g_last_status : 127;
      } else {
        status = g_last_status >= 0 ? g_last_status : 0;
      }
    }

    fflush(NULL);
    free(argv);
  }

  if(pipe_read >= 0) {
    close(pipe_read);
  }
  dup2(infd, 0);
  dup2(outfd, 1);
  close(infd);
  close(outfd);
  sh_free_tokens(toks);

  return status;
}


static void
sh_prompt(void) {
  char buf[PATH_MAX];
  char *cwd;

  if(!(cwd=getenv("PWD"))) {
    cwd = getcwd(buf, sizeof(buf));
  }

  fprintf(stdout, "%s$ ", cwd ? cwd : "(null)");
  fflush(stdout);
}


static void
sh_greet(void) {
  sce_version_t v;
  char s[1000];
  int temp = 0;

  printf("\nWelcome to ps5sshd (shell pid %d)\n\n", getpid());

  s[0] = '\0';
  if(!sceKernelGetHwModelName(s)) {
    printf("Model:   %20s\n", s);
  }
  if(!sceKernelGetProsperoSystemSwVersion(&v)) {
    printf("S/W:     %20s\n", v.str_version);
  }
  if(!sceKernelGetSocSensorTemperature(0, &temp)) {
    printf("SoC temp:               %d C\n", temp);
  }
  if(!sceKernelGetCpuTemperature(&temp)) {
    printf("CPU temp:               %d C\n", temp);
  }
  printf("CPU freq:            %4ld MHz\n",
         sceKernelGetCpuFrequency() / (1000*1000));

  printf("\nType 'help' for a list of commands\n\n");
}


int
main(int argc, char** argv) {
  struct sigaction sa;
  char *line;
  int status = 0;

  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = sh_on_interrupt;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGUSR1, &sa, NULL);
  signal(SIGPIPE, SIG_IGN);

  setvbuf(stdout, NULL, _IONBF, 0);
  setvbuf(stderr, NULL, _IONBF, 0);

  if(!getenv("PWD")) {
    setenv("PWD", "/", 1);
  }
  if(!getenv("HOME")) {
    setenv("HOME", "/", 1);
  }
  if(!getenv("PATH")) {
    setenv("PATH", "/data/ps5sshd/bin:/mnt/usb0/homebrew/bin:"
           "/user/homebrew/bin", 1);
  }
  chdir(getenv("PWD"));

  if(argc >= 3 && !strcmp(argv[1], "-c")) {
    size_t len = 0;
    char *cmd;

    for(int i=2; i<argc; i++) {
      len += strlen(argv[i]) + 1;
    }
    if(!(cmd=calloc(1, len + 1))) {
      return 1;
    }
    for(int i=2; i<argc; i++) {
      strcat(cmd, argv[i]);
      if(i + 1 < argc) {
        strcat(cmd, " ");
      }
    }
    status = sh_run_line(cmd);
    free(cmd);
    fflush(NULL);
    return status < 0 ? 1 : status;
  }

  if(!(argc >= 2 && !strcmp(argv[1], "-q"))) {
    sh_greet();
  }

  while(1) {
    sh_prompt();
    if(!(line=sh_readline())) {
      break;
    }
    status = sh_run_line(line);
    free(line);
  }

  return 0;
}
