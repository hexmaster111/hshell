#ifndef _VT100_H
#define _VT100_H

#include <termios.h>

// Gerneral Graphics API
void InitConsole();
void WriteGraphicsOut();

void DrawText(const char *text);
void DrawTextAt(int col, int row, const char *text);
void SetCurrsorPos(int col, int row);
void GetScreenSize(int *cols, int *rows);
void GetCursorPosition(int *col, int *row);
void ClearConsole();
void ClearLine();
void HomeCurrsor();
void HideCurrsor();
void ShowCurrsor();

void SetWindowPos(int t, int b);
void ScrollUp();

void StoreTerminalConfig(); // Stores the current term config
void SetSensableTerminal(); // Resotres terminal to a config most programs will run with
void RestoreTermConfig(); // Restores terminal config to one stored by <StoreTerminalConfig>


void cleanup();

typedef struct VT
{
    int israw, ifd, ofd;
    struct termios restore;
} VT, *VTPTR;

typedef enum TERM_KEY
{
    TK_READ_ERROR = -1,
    TK_KEY_NULL = 0,
    TK_CTRL_C = 3,
    TK_CTRL_D = 4,
    TK_CTRL_F = 6,
    TK_CTRL_H = 8,
    TK_TAB = 9,
    TK_CTRL_L = 12,
    TK_ENTER = 13,
    TK_CTRL_Q = 17,
    TK_CTRL_S = 19,
    TK_CTRL_U = 21,
    TK_CTRL_V = 22,
    TK_CTRL_X = 24,
    TK_CTRL_Y = 25,
    TK_CTRL_Z = 26,
    TK_ESC = 27,
    TK_BACKSPACE = 127,
    /* The following are just soft codes, not really reported by the
     * terminal directly. */
    TK_ARROW_LEFT = 1000,
    TK_ARROW_RIGHT,
    TK_ARROW_UP,
    TK_ARROW_DOWN,
    TK_DEL_KEY,
    TK_HOME_KEY,
    TK_END_KEY,
    TK_PAGE_UP,
    TK_PAGE_DOWN,
    TK_SHIFT_ARROW_LEFT,
    TK_SHIFT_ARROW_RIGHT,
    TK_SHIFT_ARROW_UP,
    TK_SHIFT_ARROW_DOWN,
    TK_CTRL_ARROW_LEFT,
    TK_CTRL_ARROW_RIGHT,
    TK_CTRL_ARROW_UP,
    TK_CTRL_ARROW_DOWN,
    TK_CTRL_SHIFT_ARROW_LEFT,
    TK_CTRL_SHIFT_ARROW_RIGHT,
    TK_CTRL_SHIFT_ARROW_UP,
    TK_CTRL_SHIFT_ARROW_DOWN,
} TERM_KEY;

TERM_KEY ReadKey();

#define RAWFLAGS_WAITFORKEYPRESS (1 << 1)

/* puts the given v->fd into raw mode
 * 0 : ok
 * 1 : fail
 */
int vt_setrawmode(VTPTR v, int flags);

/* Reads from a terminal that is in raw mode */
TERM_KEY vt_readkey(VTPTR v);

/* restores the given fd from raw mode TCSETATTR return*/
int vt_restoremode(VTPTR v);

/* returns currsor position
 * 0 : ok
 * 1 : fail
 */
int vt_getcursorpos(VTPTR v, int *rows, int *cols);

/* returns window size
 * 0 : ok
 * 1 : fail
 */
int vt_getwindowsize(VTPTR v, int *rows, int *cols);

typedef struct VT_WRITE_BUFFER
{
    char *buf;
    int len;
} VT_WB, *VT_WBPTR;

void vtwb_append(VT_WBPTR w, const char *s, int len);
void vtwb_appendfmt(VT_WBPTR w, const char *fmt, ...);
void vtwb_free(VT_WBPTR w);
#endif