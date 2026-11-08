/* The in-game menus (0x44b8f0-0x44e000; docs/re/menus.md): Esc's menu during a mission with its pages
   (Abort Mission, Play Options, Graphic Detail, Audio Control), the confirmation, and the mission's end.
   A menu is drawn once over the frame, its picture (6mainmn1.map and the others, from 6escape.pak) with
   its text on top; the play is stopped (state 0x10) while it is open, and the keys (Play_StoppedKeys
   0x44fab0) and the mouse (the window's 0x201, 0x202) drive it.

   The menus' records, their items, labels, value names and boxes are the exe's (0x4d8000-0x4db000), read
   at run time into a copy the menus change as the original changes its own. */
#pragma once
#include <stdint.h>

extern int32_t g_cheater;        /* 0x4e8a78: a cheat option was on (the play options' 0x1c); a won mission ends as 0xb */
extern int32_t g_no_shell;       /* 0x4a80e0: a mission from the command line, without the shell */
extern int32_t g_first_trip;     /* 0x58dc88: t01, t13 or t17 (no Reconfigure, the end menu's short form) */

/* GameMenu_Open 0x44b8f0 (the command 0x20 with the play running): the main menu; the play stopped */
int GameMenu_Open(void);
/* GameMenu_Close 0x44b9c0 (the main menu's end, and the command 0x20 again): the settings applied, the
   play resumed */
void GameMenu_Close(void);
/* EndMenu_Open 0x44dc80 (WinMain at the mission's end, states 0 and 0xb): 1 when it opened */
int EndMenu_Open(void);
/* Menu_Keys 0x44d3b0: the typed key (0x4defc8) as the open menu's move, change, choice or cancel */
void Menu_Keys(void);
/* Menu_Mouse 0x44d7f0: the left button's press (0x201) or release (0x202) at x, y */
void Menu_Mouse(int msg, int x, int y);
/* Menu_Show 0x44ce70: the open menu drawn over the screen and shown */
void Menu_Show(void);
/* Objects_Detail 0x424320: the entities' faces' masks from the object detail (moving ones the detail's
   byte 4, the others byte 5; 0 drops the faces' bit 0x14, the ones of classes 0x1e, 0x34, 0x3e, 0x4b
   never) */
void Objects_Detail(void);
