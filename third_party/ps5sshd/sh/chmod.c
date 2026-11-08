/* chmod for the ps5sshd shell: octal and symbolic modes, -R for recursion. */

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <sys/types.h>


extern volatile sig_atomic_t g_interrupted;


typedef struct chmod_opts {
  const char *spec;
  int         recursive;
  int         verbose;
  int         errors;
} chmod_opts_t;


static int
parse_octal(const char *spec, mode_t *mode) {
  mode_t m = 0;
  int digits = 0;

  for(const char *p=spec; *p; p++) {
    if(*p < '0' || *p > '7' || ++digits > 4) {
      return -1;
    }
    m = (m << 3) | (mode_t)(*p - '0');
  }
  if(!digits) {
    return -1;
  }
  *mode = m;

  return 0;
}


/**
 * Apply a symbolic mode such as "u+x,go-w" or "a=rwX" to old.
 * Returns 0 on success, -1 on a malformed spec.
 **/
static int
apply_symbolic(const char *spec, mode_t old, int isdir, mode_t *out) {
  const char *p = spec;
  mode_t mode = old & 07777;

  while(*p) {
    mode_t who = 0;

    for(; *p && strchr("ugoa", *p); p++) {
      switch(*p) {
      case 'u': who |= S_ISUID | S_IRWXU; break;
      case 'g': who |= S_ISGID | S_IRWXG; break;
      case 'o': who |= S_IRWXO; break;
      case 'a': who |= 07777; break;
      }
    }
    if(!who) {
      who = 07777;
    }
    if(!*p || !strchr("+-=", *p)) {
      return -1;
    }

    while(*p && strchr("+-=", *p)) {
      char op = *p++;
      mode_t perm = 0;

      for(; *p && strchr("rwxXst", *p); p++) {
        switch(*p) {
        case 'r': perm |= 0444; break;
        case 'w': perm |= 0222; break;
        case 'x': perm |= 0111; break;
        case 'X':
          if(isdir || (old & 0111)) {
            perm |= 0111;
          }
          break;
        case 's': perm |= S_ISUID | S_ISGID; break;
        case 't': perm |= S_ISVTX; break;
        }
      }
      perm &= who;

      if(op == '+') {
        mode |= perm;
      } else if(op == '-') {
        mode &= ~perm;
      } else {
        mode = (mode & ~who) | perm;
      }
    }

    if(*p == ',') {
      p++;
      if(!*p) {
        return -1;
      }
    } else if(*p) {
      return -1;
    }
  }

  *out = mode;
  return 0;
}


static void
chmod_apply(chmod_opts_t *opts, const char *path, mode_t mode) {
  if(chmod(path, mode) != 0) {
    fprintf(stderr, "chmod: %s: %s\n", path, strerror(errno));
    opts->errors++;
  } else if(opts->verbose) {
    printf("mode of '%s' changed to %04o\n", path, (unsigned)mode);
  }
}


static void
chmod_one(chmod_opts_t *opts, const char *path, int depth) {
  struct stat st;
  mode_t mode;
  int post;

  if(g_interrupted) {
    return;
  }

  if(lstat(path, &st) != 0) {
    fprintf(stderr, "chmod: %s: %s\n", path, strerror(errno));
    opts->errors++;
    return;
  }
  if(S_ISLNK(st.st_mode)) {
    return;
  }

  if(parse_octal(opts->spec, &mode) != 0 &&
     apply_symbolic(opts->spec, st.st_mode, S_ISDIR(st.st_mode), &mode)) {
    fprintf(stderr, "chmod: invalid mode: %s\n", opts->spec);
    opts->errors++;
    return;
  }

  /* a directory that loses owner read/search access is changed after its
     contents, otherwise the walk could lock itself out */
  post = opts->recursive && S_ISDIR(st.st_mode) &&
         ((st.st_mode & ~mode) & (S_IRUSR | S_IXUSR));
  if(!post) {
    chmod_apply(opts, path, mode);
  }

  if(opts->recursive && S_ISDIR(st.st_mode)) {
    struct dirent *ent;
    DIR *dir;

    if(depth > 256) {
      fprintf(stderr, "chmod: %s: directory tree too deep\n", path);
      opts->errors++;
      dir = NULL;
    } else if(!(dir=opendir(path))) {
      fprintf(stderr, "chmod: %s: %s\n", path, strerror(errno));
      opts->errors++;
    }
    while(dir && (ent=readdir(dir)) && !g_interrupted) {
      char child[PATH_MAX];

      if(!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..")) {
        continue;
      }
      if(snprintf(child, sizeof(child), "%s%s%s", path,
                  path[strlen(path) - 1] == '/' ? "" : "/", ent->d_name)
         >= (int)sizeof(child)) {
        fprintf(stderr, "chmod: %s/%s: path too long\n", path, ent->d_name);
        opts->errors++;
        continue;
      }
      chmod_one(opts, child, depth + 1);
    }
    if(dir) {
      closedir(dir);
    }
  }

  if(post) {
    chmod_apply(opts, path, mode);
  }
}


int
sh_chmod(int argc, char **argv) {
  chmod_opts_t opts;
  mode_t dummy;
  int i = 1;

  memset(&opts, 0, sizeof(opts));
  g_interrupted = 0;

  for(; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
    if(!strcmp(argv[i], "--")) {
      i++;
      break;
    }
    if(strspn(argv[i] + 1, "Rv") != strlen(argv[i] + 1)) {
      /* not an option, e.g. "-w" as a symbolic mode */
      break;
    }
    for(const char *p=argv[i] + 1; *p; p++) {
      if(*p == 'R') {
        opts.recursive = 1;
      } else {
        opts.verbose = 1;
      }
    }
  }

  if(argc - i < 2) {
    fprintf(stderr, "usage: chmod [-Rv] MODE FILE...\n"
            "  MODE is octal (755) or symbolic (u+x,go-w,a=rwX)\n");
    return 1;
  }

  opts.spec = argv[i++];
  if(parse_octal(opts.spec, &dummy) != 0 &&
     apply_symbolic(opts.spec, 0, 0, &dummy) != 0) {
    fprintf(stderr, "chmod: invalid mode: %s\n", opts.spec);
    return 1;
  }

  for(; i < argc; i++) {
    chmod_one(&opts, argv[i], 0);
  }

  if(g_interrupted) {
    g_interrupted = 0;
    fprintf(stderr, "chmod: interrupted\n");
    return 130;
  }

  return opts.errors ? 1 : 0;
}
