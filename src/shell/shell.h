/* The shell, I76SHELL.DLL's ShellMain (i76shell 0x100264c0) and ShellWindowProc (0x10025bf0) as the port
   runs them: ShellMain's set-up once, then a pass of its loop each frame; the window's messages through a
   queue of the port's (PostMessage, PostQuitMessage). docs/re/shell.md */
#pragma once
#include "shell/font.h"
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

/* the fonts (ShellMain's, by entry; 0x10068c08, 0x10068c0c, 0x10068c10, 0x10068c14, 0x10068c00) */
extern ShFont g_sh_font44, g_sh_font42, g_sh_font45, g_sh_font46, g_sh_font47;
extern Mw2 *g_sh_db;           /* 0x10068c20 */
extern ShMouse g_sh_mouse;
extern int g_sh_popup;         /* 0x10063794: the menu's pop-up to open at the next message */
extern int g_sh_popup_item;    /* 0x1008ea10 */
extern int g_sh_menu_from;     /* 0x1005c7b8: the pop-up opened from a trip's screen */
extern uint32_t g_sh_screen_msg; /* 0x1008e9e8: the message that opened the screen */
extern int g_sh_prev;          /* ShellMain's argument 11: the exe's state before the shell (1 a mission won) */
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
void Trip_Start(void);           /* i76shell 0x100017b0 (0xc022) */
void Shell_TestWon(int trip);    /* the port's: as after trip mission trip won (--param trip=n) */

/* ShellMain's set-up: prev_state the exe's state (0x4a80f4; 5 after the intro); false if the database
   cannot be opened */
bool Shell_Start(int prev_state);
/* a pass of ShellMain's loop; false once the shell has quit (its result then) */
bool Shell_Pass(void);
int Shell_Result(void);          /* 3 a mission to play, 0xff the game to end */
void Shell_End(void);            /* ShellMain's frees */

void Shell_Post(uint32_t msg, uint32_t wparam);   /* PostMessage to the shell's window */
void Shell_Quit(uint32_t code);                   /* PostQuitMessage */
/* i76shell 0x100336d0, Click_Play: a wave played and waited out (the passes held meanwhile) */
void Shell_Click(const uint8_t *wav);
/* i76shell 0x1000fe00: the box asking yes or no (entry 0x24's frame 0xb at 115, 130, text drawing what it
   asks), waited on; then the screen's redraw (g_sh_redraw) and done with the answer */
void Shell_YesNo(void (*text)(int x, int y), void (*done)(int yes));
extern void (*g_sh_redraw)(void);   /* 0x10063790 */
extern void (*g_sh_modal)(void);    /* 0x1006378c: a pop-up's pass, run in the screen's place */
void Scr_Shadow(int x, int y, int w, int h);   /* i76shell 0x1003d7c0 with a block of 0x0f */
const uint8_t *Shell_Set24(void);
void Shell_SetMenu(void (*fn)(int on));     /* i76shell 0x10026f80 */
void Shell_UnsetMenu(void (*fn)(int on));   /* i76shell 0x10026f90 */
extern int g_sh_popup_up;                   /* 0x1005c8f4 */
extern uint32_t g_sh_next_msg;              /* 0x10063798 */
int Shell_Key(void);                        /* i76shell 0x100223b0: the next typed code, 0 none */
extern int g_sh_kb_key;                     /* the keyboard object's key (0x10068bf0 +0) */
int Shell_KbRead(void);                     /* i76shell 0x10038640: 0 none, 1 a key, 3 Esc */
void Shell_KbFlush(void);                   /* i76shell 0x10038590 */
/* i76shell 0x10038d60: a line's edit at x, y in font f, buf its text (to max characters and max_w
   pixels); per pass: -2 still editing, then -1 a click (g_sh_edit_x, g_sh_edit_y), 0 Esc or a pop-up's
   end, 1 Enter or Tab */
void Edit_Begin(const ShFont *f, int x, int y, char *buf, int max, int max_w);
int Edit_Pass(void);
extern int g_sh_edit_x, g_sh_edit_y;        /* 0x10092b10, 0x10092b14 */
extern int g_sh_editing;                    /* an edit runs: the keys are its own */
void Bookmark_Save(void);                   /* i76shell 0x1001ad30, pop-up 1 */
void Bookmark_Load(void);                   /* i76shell 0x100029c0, pop-up 11 */
void Bookmark_Restore(void);
void EscMenu_Open(void);                    /* i76shell 0x1000cc40, pop-up 0 */
void QuitMenu_Open(void);
void PlayOptions_Open(void);                /* i76shell 0x10021860, pop-up 4 */
void Credits_Open(void);                    /* i76shell 0x10032790, pop-up 9 */
void Interlude_Open(void);                  /* i76shell 0x10033c60 (0xc015) */
void FinalCar_Open(void);                   /* i76shell 0x100337c0 (0xc00d) */                   /* i76shell 0x100334b0, pop-up 2 */                /* i76shell 0x10002d80 (0xc01d) */   /* entry 0x24, the boxes' frames (0x1005fae0) */
/* i76shell 0x1000fb10: note k (frame k of set 0x24), waited on until a press in its OK (x, y, w, h) */
void Shell_Note(int k, int x, int y, int w, int h);
int Rect_Hit(int x, int y, const void *rect);   /* i76shell 0x10034d20 */
void Shell_SetScreen(ShScreenFn fn);              /* i76shell 0x10026ef0 */
void Shell_UnsetScreen(ShScreenFn fn);            /* i76shell 0x10026f00 */

/* the shell's CRT rand (its own seed, from the time at ShellMain's start) */
int Shell_Rand(void);

/* the screens */
void MainMenu_Open(void);        /* i76shell 0x100236d0 (0xc00e) */
