/* PS5Term - the request PS5Term writes for ps5term-run. GPL-3.0-or-later.
 *
 * Text, one key=value per line: token, port, columns, rows, cwd, then one arg= per
 * argument (the first is the program's path). Shared by the launcher payload and the
 * host tests, so both read it the same way. */
#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

    enum
    {
        ps5term_run_max_args = 32,
        ps5term_run_max_text = 8192,
        ps5term_run_token_size = 32 /* hex characters */
    };

    typedef struct
    {
        char token[ps5term_run_token_size + 1];
        unsigned port, columns, rows;
        const char *cwd;
        int argc;
        char *argv[ps5term_run_max_args + 1]; /* NULL-terminated */
        char text[ps5term_run_max_text + 1];  /* storage for cwd and argv */
    } Ps5termRunRequest;

    /* 0 on success; -1 when anything is missing, malformed or too large. */
    int ps5term_run_parse(const char *bytes, size_t size, Ps5termRunRequest *out);

#ifdef __cplusplus
}
#endif
