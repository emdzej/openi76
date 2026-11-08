/* The port's autopilot for headless runs (--param autopilot=<leader>[,<target>...]; not the original's) */
#pragma once

void Autopilot_Frame(void);   /* after the script (AI_Frame) */
void Autopilot_Reset(void);   /* at a mission's start */
