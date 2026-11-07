/* A vehicle's engine and gearbox (0x42f4c0-0x42fad0, docs/re/vehicles.md "The engine").

   The engine's node (class 21, the vehicle data's 0x3c4) has 0x2c bytes of data: +8 the gear (1-4), +0xc
   the torque curve's width, +0x10 the torque, +0x14 the peak torque (ENGN +0xc), +0x18 its factor, +0x1c
   the revolutions per minute, +0x24 the road speed as the gauge shows it. The torque falls off as a
   parabola either side of 3500 rpm. */
#pragma once
#include <stdint.h>

/* Gear_Choose 0x42f4c0: the automatic gearbox's choice from gear g by the throttle and the speed (m/s;
   compared in km/h); mode 5 holds at most second, 4 at most third */
int Gear_Choose(float throttle, float speed, int g, int mode);

/* Engine_Step 0x42f640: the gear, the rpm and the torque for the throttle and the speed. flags is the
   vehicle's (+0x454: bit 0 the engine runs, bits 1-2 it is out of gear); neutral holds no gear; mode is
   the vehicle's +0x104 (0 and 2 keep first gear, 4 and 5 limit the gearbox) */
void Engine_Step(uint8_t *e, float throttle, float speed, uint32_t flags, int neutral, int mode);
