/* PS5Term - a PS5 program for testing `run`: plain output, a line of input, then a
 * full-screen screen with raw keys, like an ncurses program. GPL-3.0-or-later. Not shipped. */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t interrupted;
static void on_interrupt(int signal)
{
    (void)signal;
    interrupted = 1;
}

int main(int argc, char **argv)
{
    signal(SIGINT, on_interrupt);
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("term-test: %d argument(s):", argc - 1);
    for (int i = 1; i < argc; ++i)
        printf(" [%s]", argv[i]);
    printf("\nTERM=%s COLUMNS=%s LINES=%s PWD=%s\n", getenv("TERM") ? getenv("TERM") : "-",
           getenv("COLUMNS") ? getenv("COLUMNS") : "-", getenv("LINES") ? getenv("LINES") : "-",
           getenv("PWD") ? getenv("PWD") : "-");
    printf("isatty(0)=%d. Type a line and press Enter: ", isatty(0));
    char line[256];
    if (!fgets(line, sizeof line, stdin))
        return 2;
    line[strcspn(line, "\r\n")] = '\0';
    printf("You typed [%s]. Full screen next; press q to quit, Ctrl+C to interrupt.\n", line);
    sleep(2);
    int columns = getenv("COLUMNS") ? atoi(getenv("COLUMNS")) : 80;
    int rows = getenv("LINES") ? atoi(getenv("LINES")) : 24;
    printf("\033[?1049h\033[2J\033[H");
    printf("\033[1;1H+");
    printf("\033[1;%dH+", columns);
    printf("\033[%d;1H+", rows);
    printf("\033[%d;%dH+", rows, columns);
    printf("\033[%d;%dH\033[7m term-test full screen %dx%d \033[0m", rows / 2, columns / 2 - 14,
           columns, rows);
    printf("\033[%d;%dHkeys: ", rows / 2 + 2, columns / 2 - 14);
    int keys = 0;
    for (;;)
    {
        unsigned char key;
        ssize_t got = read(0, &key, 1);
        if (interrupted)
        {
            printf("\033[?1049lterm-test: interrupted after %d key(s)\n", keys);
            return 130;
        }
        if (got <= 0)
            return 3;
        if (key == 'q')
            break;
        ++keys;
        if (key >= 32 && key < 127)
            printf("%c", key);
        else
            printf("<%02x>", key);
    }
    printf("\033[?1049lterm-test: done, %d key(s) before q\n", keys);
    return 7;
}
