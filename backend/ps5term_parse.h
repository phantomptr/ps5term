// PS5Term - bounded single-command argument parser. GPL-3.0-or-later.
#pragma once
#include <stddef.h>
static inline int ps5term_parse(char *line, char **argv, size_t capacity)
{
    char *read_at = line, *write_at = line;
    size_t argc = 0;
    if (capacity == 0)
        return -1;
    while (*read_at)
    {
        while (*read_at == ' ' || *read_at == '\t')
            ++read_at;
        if (!*read_at)
            break;
        if (argc + 1 >= capacity)
            return -1;
        argv[argc++] = write_at;
        char quote = 0;
        while (*read_at)
        {
            char ch = *read_at;
            if (!quote && (ch == ' ' || ch == '\t'))
                break;
            if (!quote && (ch == '|' || ch == '&' || ch == ';' || ch == '<' || ch == '>'))
                return -1;
            ++read_at;
            if (ch == '\\' && quote != '\'')
            {
                if (!*read_at)
                    return -1;
                *write_at++ = *read_at++;
            }
            else if (ch == '\'' || ch == '"')
            {
                if (!quote)
                    quote = ch;
                else if (quote == ch)
                    quote = 0;
                else
                    *write_at++ = ch;
            }
            else
                *write_at++ = ch;
        }
        if (quote)
            return -1;
        if (*read_at)
            ++read_at;
        *write_at++ = 0;
    }
    argv[argc] = NULL;
    return (int)argc;
}
