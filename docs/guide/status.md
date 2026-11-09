# Status

OpenI76 is **in progress**. The game's subsystems are ported and run; the missions are being played
through and checked against the original. There are no releases yet; every push builds the gasm
bundles for macOS (universal), Linux (x86_64 and arm64) and Windows (x86_64) and smoke-tests each on its
own system, and they can be downloaded from the run's artifacts on GitHub Actions.

## What runs

- **The discs.** ISO 9660 images, raw `.bin` images with their `.cue`, the extracted files, and the
  `I76.ZFS` archive inside. The program is checked (size and CRC-32) and its tables are read from it.
- **The shell** (`I76SHELL.DLL`). The main menu, the training, the trip with its garage, the set-up,
  bookmarks and pop-ups, the melee's scenarios and the instant melee on their map, and the controls'
  set-up.
- **The movies.** The intro, the credits and the missions' cutscenes (Smacker), with Esc to skip them.
- **The world.** The original's 8-bit software renderer: terrain, roads, sky, objects with their levels of
  detail, dynamic lights and shadows. Checked against the original in an x86 emulator.
- **The vehicles.** Loading, the physics with the engine and gears, collisions between vehicles and with
  objects, damage, death and wrecks, the helicopters.
- **The AI.** Pathfinding (over the bridges too), driving, avoiding, attacking and the tactics, under the
  mission script's orders.
- **The weapons.** Every kind of ordnance, the turrets and the artillery, the specials (nitro, blower,
  jammer), the salvage list.
- **The effects.** Explosions, smoke, contrails, flames, skid marks, debris and the wrecks' parts.
- **The cockpit.** The 3D and the flat cockpit with their instruments, the weapons' panel, the radar, the
  damage display, the HUD's target box and the rear-view mirror.
- **The missions' scripts.** The state machines, conditions, orders, objectives, the notepad and the map.
- **Sound.** Engines, tyres, weapons, the cockpit, the radar and the CB radio's queue, placed in 3D; the
  music from disc 2's audio tracks.
- **The menus in a mission.** The Esc menu, the end of a mission, replay and exit.

## Played through

The first four missions of the trip play to a win from start to end, driven by the port's autopilot.
Every other mission's script has been run to its win too (with the port's test aids standing in for a
player's fights), and each is now being played by hand. The melee's arenas run and are being played
through. How it all works is in [How the game works](./theory.md).

- **Network play.** Hosting and joining a melee through the shell (every transport runs over the gasm
  relay), the players' cars, shots, kills, scores, respawns, joins approved by the host, the chat and the
  names over the cars.

## Not ported yet

Nothing of the original is left unported; the remaining work is checking each mission's play through.

## Beyond the original

Additions come after the faithful game and are always opt-in: the default is the original.

- **A GL renderer** (being built; a second module, `openi76-gl.wasm`, run with `./play --gl`, not on the
  play page yet). It presents the 8-bit frame through its palette with a shader, the same picture as the
  software path. With `--param glpoly=1` GL also draws the 3D polygons into an index framebuffer: every
  filler but one, with the translucency, glow, remap and shadow tables done in shaders. Its pixels are
  used wherever nothing 2D was drawn over them. At the original's resolution the picture is the software one but for about 1% of pixels, the
  rasterizers' texel rounding. On top of it, each opt-in: `--param glscale=<N>` (or `fit`, the window's
  size) draws the 3D polygons at N times the resolution (the 2D art, the sky and the few polygons GL does not
  draw stay at the original's); `--param glfilter=1` smooths the textures and the shading in 24-bit colour;
  `--param gldepth=1` adds a depth buffer, so polygons the game's sorting got wrong no longer show through.
