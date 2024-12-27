#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <string.h>
#include <stdarg.h>

#include "vt100.h"


static VT g_term = {.ifd = STDIN_FILENO, .ofd = STDOUT_FILENO};

static VT g_storedtermconfig = {.ifd = STDIN_FILENO, .ofd = STDOUT_FILENO};

static VT_WB g_outbuffer;

// flicker free console api
TERM_KEY ReadKey() { return vt_readkey(&g_term); }
void ClearConsole() { vtwb_append(&g_outbuffer, "\033[2J", 4); }

void ClearLine() { vtwb_appendfmt(&g_outbuffer, "\033[2K"); }
void HomeCurrsor() { vtwb_append(&g_outbuffer, "\x1b[H", 3); }
void HideCurrsor() { vtwb_append(&g_outbuffer, "\x1b[?25l", 6); }
void ShowCurrsor() { vtwb_append(&g_outbuffer, "\x1b[?25h", 6); }
void SetCurrsorPos(int col, int row) { vtwb_appendfmt(&g_outbuffer, "\x1b[%d;%dH", row, col); }

// Restores terminal config to one stored by <StoreTerminalConfig>
void RestoreTermConfig(){
    tcsetattr(g_term.ifd, TCSAFLUSH, &g_storedtermconfig.restore);
} 

// Stores the current term config
void StoreTerminalConfig(){
    tcgetattr(g_term.ifd, &g_storedtermconfig.restore);
}

// Resotres terminal to a config most programs will run with
void SetSensableTerminal(){
    tcsetattr(g_term.ifd, TCSAFLUSH, &g_term.restore);
} 


void GetCursorPosition(int *col, int *row)
{
    if (vt_getcursorpos(&g_term, row, col))
    {
        perror("Get Cursor Position");
        exit(EXIT_FAILURE);
    }
}

void GetScreenSize(int *cols, int *rows)
{
    if (vt_getwindowsize(&g_term, rows, cols))
    {
        perror("Get Window Size");
        exit(EXIT_FAILURE);
    }
}

void WriteGraphicsOut()
{
    write(g_term.ofd, g_outbuffer.buf, g_outbuffer.len);
    vtwb_free(&g_outbuffer);
}

void DrawText(const char *text) { vtwb_append(&g_outbuffer, text, strlen(text)); }

void DrawTextAt(int col, int row, const char *text)
{
    SetCurrsorPos(col, row);
    DrawText(text);
}

void cleanup()
{
    vt_restoremode(&g_term);
    vtwb_free(&g_outbuffer);
}

void InitConsole()
{
    if (vt_setrawmode(&g_term, 0))
        return;

    // atexit(cleanup);
}

// Lower level console api

enum
{
    OK = 0,
    FAIL = 1
};

void vtwb_append(VT_WBPTR wb, const char *s, int len)
{
    char *new = realloc(wb->buf, wb->len + len);

    if (new == NULL)
        return;

    memcpy(new + wb->len, s, len);
    wb->buf = new;
    wb->len += len;
}

void vtwb_appendfmt(VT_WBPTR wb, const char *fmt, ...)
{
    va_list ap;
    int n = 0;
    size_t size = 0;
    char *p = NULL;

    /* determin required size */
    va_start(ap, fmt);
    n = vsnprintf(p, size, fmt, ap);
    va_end(ap);

    if (n < 0)
        return; /* nothing todo, there where no bytes to add */

    size = (size_t)n + 1;

    char *new = realloc(wb->buf, wb->len + size);
    if (new == NULL)
        return; /* error getting more space! */

    va_start(ap, fmt);
    vsnprintf(new + wb->len, size, fmt, ap);
    va_end(ap);

    wb->len += size;
    wb->buf = new;
}

void vtwb_free(VT_WBPTR wb)
{
    free(wb->buf);
    wb->buf = NULL;
    wb->len = 0;
}

int vt_getcursorpos(VTPTR v, int *rows, int *cols)
{
    char buff[32];
    unsigned int i = 0;

    if (write(v->ofd, "\x1b[6n", 4) != 4)
        return FAIL;

    while (i < sizeof(buff) - 1)
    {
        if (read(v->ifd, buff + i, 1) != 1)
            break;

        if (buff[i] == 'R')
            break;
        i++;
    }
    buff[i] = '\0';

    if (buff[0] != TK_ESC || buff[1] != '[')
        return FAIL;

    if (sscanf(buff + 2, "%d;%d", rows, cols) != 2)
        return FAIL;

    return OK;
}

int vt_getwindowsize(VTPTR v, int *rows, int *cols)
{
    struct winsize ws;

    if (ioctl(v->ifd, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
    {
        int orgr, orgc;

        if (vt_getcursorpos(v, &orgr, &orgc))
            goto FAIL;

        if (write(v->ofd, "\x1b[999C\x1b[999B", 12) != 12)
            goto FAIL;

        if (vt_getcursorpos(v, rows, cols))
            goto FAIL;

        char buf[32];
        snprintf(buf, 32, "\x1b[%d;[%dH", orgr, orgc);
        if (write(v->ofd, buf, strlen(buf)) == -1)
        {
            /* cant really do much from this failure... */
        }

        return OK;
    }
    else
    {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return OK;
    }

FAIL:
    return FAIL;
}

int vt_setrawmode(VTPTR v, int flags)
{
    struct termios raw;

    if (v->israw)
        return 0; /* this VT is already in raw */

    if (!isatty(v->ifd))
        goto FAIL;

    if (tcgetattr(v->ifd, &v->restore) == -1)
        goto FAIL;

    raw = v->restore;

    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~OPOST;
    raw.c_cflag |= CS8;
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(v->ifd, TCSAFLUSH, &raw) < 0)
        goto FAIL;

    v->israw = 1;

    return 0;

FAIL:
    return 1;
}

int vt_restoremode(VTPTR v)
{
    if (!v->israw)
        return FAIL;

    int ignored = tcsetattr(v->ifd, TCSAFLUSH, &v->restore);

    return OK;
}

TERM_KEY vt_readkey(VTPTR v)
{
    if (v->israw == 0)
        return TK_READ_ERROR;

    int nread;
    char c, seq[5];
    while ((nread = read(v->ifd, &c, 1)) == 0)
        ;

    if (nread == -1)
        return TK_READ_ERROR;

    while (1)
    {

        switch (c)
        {
        case TK_ESC: /* escape sequence */

            /* If this is just an ESC, we'll timeout here. */
            if (read(v->ifd, seq, 1) == 0)
                return TK_ESC;
            if (read(v->ifd, seq + 1, 1) == 0)
                return TK_ESC;

            /* ESC [ sequences. */
            if (seq[0] == '[')
            {
                if (seq[1] >= '0' && seq[1] <= '9')
                {
                    /* Extended escape, read additional byte. */
                    if (read(v->ifd, seq + 2, 1) == 0)
                        return TK_ESC;
                    if (seq[2] == '~')
                    {
                        switch (seq[1])
                        {
                        case '3':
                            return TK_DEL_KEY;
                        case '5':
                            return TK_PAGE_UP;
                        case '6':
                            return TK_PAGE_DOWN;
                        }
                    }

                    if (seq[2] == ';')
                    {

                        if (read(v->ifd, seq + 3, 1) == 0)
                            return TK_ESC;

                        if (seq[3] == '2')
                        {
                            if (read(v->ifd, seq + 4, 1) == 0)
                                return TK_ESC;

                            switch (seq[4])
                            {
                            case 'A':
                                return TK_SHIFT_ARROW_UP;
                            case 'B':
                                return TK_SHIFT_ARROW_DOWN;
                            case 'C':
                                return TK_SHIFT_ARROW_RIGHT;
                            case 'D':
                                return TK_SHIFT_ARROW_LEFT;
                            }
                        }
                        else if (seq[3] == '5')
                        {
                            if (read(v->ifd, seq + 4, 1) == 0)
                                return TK_ESC;

                            switch (seq[4])
                            {
                            case 'A':
                                return TK_CTRL_ARROW_UP;
                            case 'B':
                                return TK_CTRL_ARROW_DOWN;
                            case 'C':
                                return TK_CTRL_ARROW_RIGHT;
                            case 'D':
                                return TK_CTRL_ARROW_LEFT;
                            }
                        }
                        else if (seq[3] == '6')
                        {
                            if (read(v->ifd, seq + 4, 1) == 0)
                                return TK_ESC;

                            switch (seq[4])
                            {
                            case 'A':
                                return TK_CTRL_SHIFT_ARROW_UP;
                            case 'B':
                                return TK_CTRL_SHIFT_ARROW_DOWN;
                            case 'C':
                                return TK_CTRL_SHIFT_ARROW_RIGHT;
                            case 'D':
                                return TK_CTRL_SHIFT_ARROW_LEFT;
                            }
                        }
                    }
                }
                else
                {

                    switch (seq[1])
                    {
                    case 'A':
                        return TK_ARROW_UP;
                    case 'B':
                        return TK_ARROW_DOWN;
                    case 'C':
                        return TK_ARROW_RIGHT;
                    case 'D':
                        return TK_ARROW_LEFT;
                    case 'H':
                        return TK_HOME_KEY;
                    case 'F':
                        return TK_END_KEY;
                    }
                }
            }

            /* ESC O sequences. */
            else if (seq[0] == 'O')
            {
                switch (seq[1])
                {
                case 'H':
                    return TK_HOME_KEY;
                case 'F':
                    return TK_END_KEY;
                }
            }

            break;
        default:
            return c;
        }
    }
}