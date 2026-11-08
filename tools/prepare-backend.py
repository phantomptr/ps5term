#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Prepare an auditable PS5Term adaptation; upstream source stays intact."""
from pathlib import Path
import shutil
root = Path(__file__).resolve().parents[1]
out = root / 'build/backend'
if out.exists():
    shutil.rmtree(out)
shutil.copytree(root / 'third_party/shsrv', out)
p = out / 'Makefile'
s = p.read_text()
a = s.index('SUBDIRS :=')
b = s.index('\nTOPTARGETS', a)
s = s[:a] + 'SUBDIRS := bundles/core\n' + s[b:]
a = s.index('\t      -Wl,--whole-archive bundles/http2_get')
b = s.index('\n\nsh.elf.inc', a)
s = s[:a].rstrip(' \\\n') + s[b:]
p.write_text(s)
p = out / 'shsrv.c'
s = p.read_text().replace('int port = 2323;', 'int port = 2324;').replace('"shsrv.elf"', '"ps5term-sh"')
a = s.index('  char ip[INET_ADDRSTRLEN];')
b = s.index('  socklen_t addr_len;', a)
s = s[:a] + '  char* argv[] = {"sh", 0};\n  (void)notify_user;\n' + s[b:]
a = s.index('  if(getifaddrs(')
b = s.index('  if((srvfd=socket(', a)
s = s[:a] + s[b:]
s = s.replace('htonl(INADDR_ANY)', 'htonl(INADDR_LOOPBACK)')
p.write_text(s)
p = out / 'sh.c'
s = p.read_text().replace('calloc(bufsize, sizeof(char));', 'calloc(bufsize, sizeof(char*));')
a = s.index('static void\nsh_greet(void)')
b = s.index('\n\nstatic void*\nsh_thread', a)
s = s[:a] + '''static void
sh_greet(void) {

}
''' + s[b:]
# Keep one complete argv with quoting. Reject unsupported operators rather than
# silently interpreting a semicolon as a pipe or deadlocking a sequential pipeline.
s = s.replace('#include "builtin.h"', '#include "builtin.h"\n#include "ps5term_parse.h"')
a = s.index('  int pipefd[2] =', s.index('static void*\nsh_thread'))
b = s.index('\n  exit(0);\n}', a)
s = s[:a] + '''  char *line = NULL;
  char *args[128];
  chdir("/data");
  setenv("HOME", "/data", 1);
  unsetenv("PWD");
  setenv("PATH", "/mnt/usb0/homebrew/bin:/user/homebrew/bin", 0);
  sh_greet();
  while(1) {
    sh_prompt();
    if(!(line=sh_readline())) exit(0);
    int argc = ps5term_parse(line, args, 128);
    if(argc < 0) {
      fprintf(stderr, "Unsupported syntax: use one command; quotes and escapes are supported.\\n");
    } else if(argc) {
      pid_t pid = sh_execute(args);
      if(pid > 0) {state->pid = pid; sh_waitpid(pid); state->pid = -1;}
    }
    free(line);
  }
''' + s[b:]
s = s.replace('  telnet_client_state_t state;', '  telnet_client_state_t state = {0};\n  state.pid = -1;')
s = s.replace('    sprintf(path, "%s/%s", paths[i], name);', '    if(snprintf(path, PATH_MAX, "%s/%s", paths[i], name) >= PATH_MAX) continue;')
s = s.replace('    strcpy(path, name);', '    if(strlen(name) >= PATH_MAX) return -1;\n    strcpy(path, name);')
s = s.replace('    return 0;\n  }\n\n  for(int i=0; paths[i];', '    return -1;\n  }\n\n  for(int i=0; paths[i];')
p.write_text(s)
shutil.copy2(root / 'backend/ps5term_parse.h', out / 'ps5term_parse.h')
# Reproducible build identification instead of wall-clock timestamps.
for p in out.rglob('*.c'):
    s = p.read_text().replace('__DATE__', '"2026-10-31"').replace('__TIME__', '"00:00:00"')
    p.write_text(s)
