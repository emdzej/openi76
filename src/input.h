/* The input module (0x47eee0-0x482e80; docs/re/input.md): the devices, the controls that input.map binds
   to the game's actions, the keys that gamekey.map binds to the game's commands, and the user's vehicle
   driven by the actions.

   - The actions (the table 0x4e6340, 83 of 0x20 bytes, read from the exe) each name a state in
     0x4def70-0x4df010 (here g_input): kind 0 an analog value (an int, 16.16 for the throttle and the
     steering), 1 a byte while held, 2 a byte on the press.
   - input.map binds each action to a chord of a device's channels ("+" pressed, "-" released, at most
     8); an analog action takes one analog channel (or ramps through its _plus, _minus, _reset and _set
     actions) into a sink (0x4e2238) that scales it to the action's range.
   - The devices: the keyboard (a 128-bit set of scan codes), the mouse and the joysticks (0x581ad0, at
     most 5). A device input.map names that is not there is listed absent and its bindings ignored. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* The actions' states, 0x4def70-0x4df010: INPUT_AT(0x4defd0) is throttle_up's byte */
#define INPUT_BASE 0x4def70u
extern uint8_t g_input[0xa0];
#define INPUT_AT(va) (&g_input[(va) - INPUT_BASE])
/* 0x4defc8: the frame's typed key (a code with Control 0x100, Shift 0x200, Alt 0x400), 0 for none */
#define INPUT_TYPED 0x4defc8u

extern int32_t g_input_menu;    /* 0x4df024: 1 runs every binding, 0 only the "menu_" ones (0x480680/0x480690) */
extern int32_t g_input_held;    /* 0x4df014: set by the menus' poll (0x480720); the next update drops the keys */
extern int32_t g_input_main;    /* 0x4df010: the menu's key (0x20, MAIN_MENU) was typed */

/* Input_Reset 0x480520: the devices closed, every table emptied, the drivers listed (keyboard, mouse,
   joystick) */
void Input_Reset(void);
/* View_Reset 0x480920: the combat view's toggle (V) set up for a mission: the cockpit, the other view F2 */
void View_Reset(void);
/* Input_Init 0x480250: input.map (and each device's giddi\<name>.std) and gamekey.map read, the keyboard
   opened and its queue emptied. False without the action table (no exe) */
bool Input_Init(void);
/* Input_Keys (the port's, for the window's messages 0x480f60): the platform's keys since the last call
   as WM_KEYDOWN and WM_KEYUP: the keyboard's set (0x4810e0) and its typed keys (0x481030) */
void Input_Keys(void);
/* the window's left button (WM_LBUTTONDOWN, WM_LBUTTONUP): a movie skipped, the menu's click */
void Input_Mouse(void);
/* the port's: the buttons as they are taken as already seen (a press the shell's window had as its message
   is not the game's: the button still held when the shell ends) */
void Input_MouseSync(void);
/* the in-game cursor's capture (0x47ba00), its release (0x47ba70) and a capture asked again (0x47b9f0) */
void Input_MouseCapture(void);
void Input_MouseRelease(void);
void Input_MouseRecapture(void);
/* a DIK code's Windows virtual key (the US layout, NumLock on), and MapVirtualKey(vk, MAPVK_VK_TO_CHAR)
   for the keys the exe's and the shell's tables leave out */
int Input_DikVk(int c);
int Input_VkChar(int vk);
/* Kb_Clear 0x4812b0: the keyboard's queue of typed keys emptied */
void Kb_Clear(void);
/* Input_Update 0x480290, each frame: the analog sinks' resets, the actions cleared, every present device
   polled, the chords evaluated into the actions, the analog values ramped and scaled; the typed key */
void Input_Update(void);
/* the exe's tables (the keys' codes the window's procedure reads), before Input_Init */
bool Input_Tables(void);
/* Input_MenuPoll 0x480720: the menus' poll; 1 when the menu key was typed */
int Input_MenuPoll(void);
/* Input_Poll 0x480870, each frame of play: the cheats, the typed key's command (0x4809b0), the user's
   vehicle's controls (0x481d00) */
void Input_Poll(void);
/* the command bound to a typed key (0x4806a0), 0 for none */
int Input_GameKey(int16_t key);
/* Cockpit_Views 0x481c10: the pilot's and the dashboard's views' glances (their keys), the windows and the
   wind outside (docs/re/input.md "The cockpit's views") */
void Cockpit_Views(void);
/* User_ControlsReset 0x481be0: the key ramps' timers at 0, the cockpit's views' state (glance 2, ahead) */
void User_ControlsReset(void);
/* User_Controls 0x481d00: the actions into the user's vehicle's data: the throttle (+0xe4) and the
   steering (+0xe0) from the analog values, or with the keys from ramps over 3 s; the engine (+0xec), the
   lights (+0x100), the hand brake (+0xf0), the gears (+0x104) and the direction (+0xe8) */
void User_Controls(void);
/* 0x581ebc: the last view was preset 1 (the cockpit); 0x581ec0: the last other preset (0x480990, 0x4809a0
   set them) */
extern int32_t g_view_cockpit, g_view_preset;
