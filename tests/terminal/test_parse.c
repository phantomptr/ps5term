/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "ps5term_parse.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    char *args[128];
    char line[] = "cp 'file one' \"file two\"";
    assert(ps5term_parse(line, args, 128) == 3);
    assert(!strcmp(args[1], "file one"));
    assert(!strcmp(args[2], "file two"));
    char escaped[] = "echo a\\ b ''";
    assert(ps5term_parse(escaped, args, 128) == 3);
    assert(!strcmp(args[1], "a b"));
    assert(!strcmp(args[2], ""));
    char bad[] = "ls | grep txt";
    assert(ps5term_parse(bad, args, 128) < 0);
    char bad2[] = "rm 'unfinished";
    assert(ps5term_parse(bad2, args, 128) < 0);
    char longline[1024] = "echo ";
    for (int i = 0; i < 130; i++)
        strcat(longline, "x ");
    assert(ps5term_parse(longline, args, 128) < 0);
    puts("Shell argument parsing checks passed");
}
