/* The port's autopilot for headless runs (--param autopilot=<leader>[,<target>...]; not the original's) */
#pragma once

void Autopilot_Frame(void);   /* after the script (AI_Frame) */
void Autopilot_Reset(void);   /* at a mission's start */
void Autopilot_Keys(void);    /* after the input, before the script: a script camera skipped as by Esc */
/* --param apinvincible=1 (the port's, with the autopilot): the user's car takes no damage and is not killed (a fall,
   a flip), so a run tests the mission's script rather than the autopilot's driving; read by Vehicle_Damage
   and Vehicle_Kill */
extern int g_ap_invincible;
extern int g_ap_strong;   /* --param apstrong=N: the user's hits do N times their damage (a test aid, off by default) */
