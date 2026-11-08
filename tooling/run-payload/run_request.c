/* PS5Term - the request PS5Term writes for ps5term-run. GPL-3.0-or-later. */
#include "run_request.h"
#include <string.h>

static int number(const char *text, unsigned limit, unsigned *out)
{
    unsigned long value = 0;
    if (!*text)
        return -1;
    for (; *text; ++text)
    {
        if (*text < '0' || *text > '9')
            return -1;
        value = value * 10 + (unsigned long)(*text - '0');
        if (value > limit)
            return -1;
    }
    *out = (unsigned)value;
    return 0;
}

int ps5term_run_parse(const char *bytes, size_t size, Ps5termRunRequest *out)
{
    memset(out, 0, sizeof *out);
    if (!bytes || size > ps5term_run_max_text || memchr(bytes, '\0', size))
        return -1;
    memcpy(out->text, bytes, size);
    out->text[size] = '\0';
    int have_token = 0, have_port = 0, have_columns = 0, have_rows = 0;
    for (char *line = out->text; line && *line;)
    {
        char *end = strchr(line, '\n');
        if (end)
            *end = '\0';
        char *equals = strchr(line, '=');
        if (!equals)
            return -1;
        *equals = '\0';
        const char *key = line, *value = equals + 1;
        if (!strcmp(key, "token"))
        {
            if (strlen(value) != ps5term_run_token_size ||
                strspn(value, "0123456789abcdef") != ps5term_run_token_size)
                return -1;
            memcpy(out->token, value, ps5term_run_token_size + 1);
            have_token = 1;
        }
        else if (!strcmp(key, "port"))
            have_port = !number(value, 65535, &out->port) && out->port;
        else if (!strcmp(key, "columns"))
            have_columns = !number(value, 500, &out->columns) && out->columns >= 2;
        else if (!strcmp(key, "rows"))
            have_rows = !number(value, 200, &out->rows) && out->rows >= 2;
        else if (!strcmp(key, "cwd"))
            out->cwd = value;
        else if (!strcmp(key, "arg"))
        {
            if (out->argc >= ps5term_run_max_args)
                return -1;
            out->argv[out->argc++] = (char *)value;
        }
        else
            return -1;
        line = end ? end + 1 : NULL;
    }
    out->argv[out->argc] = NULL;
    if (!have_token || !have_port || !have_columns || !have_rows || !out->cwd ||
        out->cwd[0] != '/' || out->argc < 1 || out->argv[0][0] != '/')
        return -1;
    return 0;
}
