#pragma once

#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssh/ssh.h>

/**
 * Run an interactive shell (cmd == NULL) or a single command line over the
 * session channel. Returns the exit status of the shell.
 **/
int shell_run(WOLFSSH *ssh, int fd, const char *cmd);
