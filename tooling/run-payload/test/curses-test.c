/* PS5Term - an ncurses program for testing `run`: a box, colours, the size, arrow keys,
 * q to quit. GPL-3.0-or-later. Not shipped; built against a local ncurses 6.4. */
#include <curses.h>
#include <errno.h>
#if defined(__PROSPERO__)
#include <ps5/klog.h>
#else
#define klog_printf(...) ((void)0) /* host runs of this test */
#endif
#include <string.h>
#include <termios.h>

int main(void)
{
    struct termios probe;
    int got = tcgetattr(0, &probe);
    klog_printf("curses-test: tcgetattr(0)=%d errno=%s\n", got, got ? strerror(errno) : "-");
    initscr();
    int c = cbreak(), e = noecho(), k = keypad(stdscr, TRUE);
    klog_printf("curses-test: cbreak=%d noecho=%d keypad=%d\n", c, e, k);
    if (has_colors())
    {
        start_color();
        init_pair(1, COLOR_CYAN, COLOR_BLACK);
        init_pair(2, COLOR_BLACK, COLOR_YELLOW);
    }
    box(stdscr, 0, 0);
    attron(COLOR_PAIR(2) | A_BOLD);
    mvprintw(1, 2, " ncurses on PS5Term: %dx%d, TERM=%s, colours=%d ", COLS, LINES, termname(),
             COLORS);
    attroff(COLOR_PAIR(2) | A_BOLD);
    attron(COLOR_PAIR(1));
    mvprintw(3, 2, "Arrow keys move the @, q quits.");
    attroff(COLOR_PAIR(1));
    int y = LINES / 2, x = COLS / 2, keys = 0;
    for (;;)
    {
        mvaddch(y, x, '@');
        mvprintw(LINES - 2, 2, "keys: %d   last at %d,%d  ", keys, y, x);
        refresh();
        int key = getch();
        klog_printf("curses-test: getch=%d\n", key);
        if (key == 'q' || key == ERR)
            break;
        ++keys;
        mvaddch(y, x, ' ');
        if (key == KEY_UP && y > 5)
            --y;
        else if (key == KEY_DOWN && y < LINES - 3)
            ++y;
        else if (key == KEY_LEFT && x > 1)
            --x;
        else if (key == KEY_RIGHT && x < COLS - 2)
            ++x;
    }
    endwin();
    printf("curses-test: %d key(s)\n", keys);
    return keys;
}
