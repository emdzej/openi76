/* The helicopters, class 9 (docs/re/vehicles.md "The helicopters") */
#pragma once
#include "scene.h"

/* Heli_Frame 0x4314a0 (class 9's frame): the flight (0x431de0) and, when it ends on the ground, the crash:
   the explosions, the parts' damage, the chunks and smoke, the weapons, the AI told */
void Heli_Frame(Node *n);
