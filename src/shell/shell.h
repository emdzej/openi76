/* The shell, I76SHELL.DLL's ShellMain (i76shell 0x100264c0) and ShellWindowProc (0x10025bf0) as the port
   runs them: ShellMain's set-up once, then a pass of its loop each frame; the window's messages through a
   queue of the port's (PostMessage, PostQuitMessage). docs/re/shell.md */
#pragma once
#include "shell/mw2.h"
#include <stdbool.h>
#include <stdint.h>

/* a screen's function (registered with 0x10026ef0): each pass with 0xc004, then the messages its menu
   gets (0xc002 at the end) */
typedef void (*ShScreenFn)(uint32_t msg);

typedef struct {               /* the mouse (i76shell 0x1001f1c0, the object at 0x10068bf8) */
    int printed;               /* +0xc: the width of the coordinates printed (the debug's) */
    int click;                 /* +0x10: 1 the left button's press, 2 held after it, 0 else */
    int rclick, mclick;        /* +0x14, +0x18: the right's and middle's presses */
    int dbl;                   /* +0x1c: a press within 200 ms of the last */
    double last_ms;            /* +0x20 */
    int x, y;                  /* +0x2c, +0x30 */
    int left, right, middle;   /* +0x34, +0x38, +0x3c: held */
    int on;                    /* +0x40: read from the pointer */
} ShMouse;

extern Mw2 *g_sh_db;           /* 0x10068c20 */
extern ShMouse g_sh_mouse;
extern int g_sh_popup;         /* 0x10063794: the menu's pop-up to open at the next message */
extern int g_sh_popup_item;    /* 0x1008ea10 */
extern uint32_t g_sh_screen_msg; /* 0x1008e9e8: the message that opened the screen */
extern int g_sh_mode;          /* 0x1008e9fc: the melee's or trip's kind the screens pass on */

/* the exe's records ShellMain is given (its arguments 7, 10, 12): the trip's mission number
   (0x4a80dc), the mission record (WinMain's, +0x5d its file), the game record (0x58d820: +0x20 the
   kind of game, +0x29 the user's vehicle file) */
typedef struct {
    int32_t trip;              /* 0x4a80dc */
    char mission[64];          /* the mission record's +0x5d */
    int32_t kind;              /* 0x58d840: 2 a trip's or training's, 3, 4, 5 network play */
    char vehicle[32];          /* 0x58d849 */
} ShellRecords;
extern ShellRecords g_sh_rec;

void Training_Start(void);       /* i76shell 0x10001680 */

/* ShellMain's set-up: prev_state the exe's state (0x4a80f4; 5 after the intro); false if the database
   cannot be opened */
bool Shell_Start(int prev_state);
/* a pass of ShellMain's loop; false once the shell has quit (its result then) */
bool Shell_Pass(void);
int Shell_Result(void);          /* 3 a mission to play, 0xff the game to end */
void Shell_End(void);            /* ShellMain's frees */

void Shell_Post(uint32_t msg, uint32_t wparam);   /* PostMessage to the shell's window */
void Shell_Quit(uint32_t code);                   /* PostQuitMessage */
void Shell_SetScreen(ShScreenFn fn);              /* i76shell 0x10026ef0 */
void Shell_UnsetScreen(ShScreenFn fn);            /* i76shell 0x10026f00 */

/* the shell's CRT rand (its own seed, from the time at ShellMain's start) */
int Shell_Rand(void);

/* the screens */
void MainMenu_Open(void);        /* i76shell 0x100236d0 (0xc00e) */
