/* rm for the ps5sshd shell: -r/-R recursive, -f force, -d empty dirs, -v. */

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/types.h>


extern volatile sig_atomic_t g_interrupted;


typedef struct rm_opts {
  int recursive;
  int force;
  int dirs;
  int verbose;
  int errors;
} rm_opts_t;


static void
rm_error(rm_opts_t *opts, const char *path, int err) {
  if(opts->force && err == ENOENT) {
    return;
  }
  fprintf(stderr, "rm: %s: %s\n", path, strerror(err));
  opts->errors++;
}


static void
rm_done(rm_opts_t *opts, const char *path) {
  if(opts->verbose) {
    printf("removed '%s'\n", path);
  }
}


/* Read all entry names first so deleting does not disturb readdir(). */
static char**
list_dir(const char *path, size_t *count) {
  struct dirent *ent;
  char **names = NULL;
  size_t n = 0, cap = 0;
  DIR *dir;

  if(!(dir=opendir(path))) {
    return NULL;
  }
  while((ent=readdir(dir))) {
    if(!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..")) {
      continue;
    }
    if(n == cap) {
      char **tmp = realloc(names, (cap = cap ? cap * 2 : 32) * sizeof(char*));
      if(!tmp) {
        break;
      }
      names = tmp;
    }
    if(!(names[n]=strdup(ent->d_name))) {
      break;
    }
    n++;
  }
  closedir(dir);

  if(!names) {
    names = calloc(1, sizeof(char*));
  }
  *count = n;

  return names;
}


static void
rm_path(rm_opts_t *opts, const char *path, int depth) {
  struct stat st;

  if(g_interrupted) {
    return;
  }

  if(lstat(path, &st) != 0) {
    rm_error(opts, path, errno);
    return;
  }

  if(!S_ISDIR(st.st_mode)) {
    if(unlink(path) != 0) {
      rm_error(opts, path, errno);
    } else {
      rm_done(opts, path);
    }
    return;
  }

  if(opts->recursive) {
    size_t count = 0;
    char **names;

    if(depth > 256) {
      fprintf(stderr, "rm: %s: directory tree too deep\n", path);
      opts->errors++;
      return;
    }
    if(!(names=list_dir(path, &count))) {
      rm_error(opts, path, errno);
      return;
    }
    for(size_t i=0; i<count; i++) {
      char child[PATH_MAX];

      if(!g_interrupted) {
        if(snprintf(child, sizeof(child), "%s%s%s", path,
                    path[strlen(path) - 1] == '/' ? "" : "/", names[i])
           >= (int)sizeof(child)) {
          fprintf(stderr, "rm: %s/%s: path too long\n", path, names[i]);
          opts->errors++;
        } else {
          rm_path(opts, child, depth + 1);
        }
      }
      free(names[i]);
    }
    free(names);
    if(g_interrupted) {
      return;
    }
  } else if(!opts->dirs) {
    fprintf(stderr, "rm: %s: is a directory (use -r)\n", path);
    opts->errors++;
    return;
  }

  if(rmdir(path) != 0) {
    rm_error(opts, path, errno);
  } else {
    rm_done(opts, path);
  }
}


/**
 * Refuse to recursively delete "/", top-level directories such as /data or
 * /user, and "." or "..", which are almost certainly typos and could wipe
 * console data.
 **/
static int
is_protected(const char *path) {
  char resolved[PATH_MAX];
  const char *base;
  int components = 0;

  base = strrchr(path, '/');
  base = base ? base + 1 : path;
  if(!strcmp(base, ".") || !strcmp(base, "..")) {
    return 1;
  }

  if(!realpath(path, resolved)) {
    return 0;
  }
  for(const char *p=resolved; *p; p++) {
    if(*p == '/' && p[1] && p[1] != '/') {
      components++;
    }
  }

  return components <= 1;
}


int
sh_rm(int argc, char **argv) {
  rm_opts_t opts;
  int i = 1;

  memset(&opts, 0, sizeof(opts));
  g_interrupted = 0;

  for(; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
    if(!strcmp(argv[i], "--")) {
      i++;
      break;
    }
    for(const char *p=argv[i] + 1; *p; p++) {
      switch(*p) {
      case 'r':
      case 'R': opts.recursive = 1; break;
      case 'f': opts.force = 1; break;
      case 'd': opts.dirs = 1; break;
      case 'v': opts.verbose = 1; break;
      default:
        fprintf(stderr, "rm: unknown option -%c\n"
                "usage: rm [-rfdv] FILE...\n", *p);
        return 1;
      }
    }
  }

  if(i >= argc) {
    if(opts.force) {
      return 0;
    }
    fprintf(stderr, "usage: rm [-rfdv] FILE...\n");
    return 1;
  }

  for(; i < argc && !g_interrupted; i++) {
    if(opts.recursive && is_protected(argv[i])) {
      fprintf(stderr, "rm: refusing to remove '%s' recursively\n", argv[i]);
      opts.errors++;
      continue;
    }
    rm_path(&opts, argv[i], 0);
  }

  if(g_interrupted) {
    g_interrupted = 0;
    fprintf(stderr, "rm: interrupted\n");
    return 130;
  }

  return opts.errors ? 1 : 0;
}
