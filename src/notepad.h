/* The notepad (0x41e950, Notepad_Init; docs/re/fsm.md "The objectives"): its key shows the mission's
   objectives not hidden, a succeeded one struck through, written with the notepad's font over zhpd.map, the
   play stopped (state 8) with the sounds paused until a key closes it. The map (0x44f7f0) shows the
   mission's zmap picture the same way (state 4). */
#pragma once

/* 0x41e950: opened (the state 8), or closed when it is open */
void Notepad_Toggle(void);
/* 0x44f7f0: the mission's map shown (the state 4), or put away when it is shown */
void Map_Toggle(void);
