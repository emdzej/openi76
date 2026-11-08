/* The notepad (0x41e950, Notepad_Init; docs/re/fsm.md "The objectives"): its key shows the mission's
   objectives not hidden, a succeeded one struck through, written with the notepad's font over zhpd.map, the
   play stopped (state 8) with the sounds paused until a key closes it. */
#pragma once

/* 0x41e950: opened (the state 8), or closed when it is open */
void Notepad_Toggle(void);
