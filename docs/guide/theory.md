# How Interstate '76 works

A theory of operation of Interstate '76 (Activision, 1997): how the game is built, from the frame loop
to the missions. It comes from porting the Windows executable and its front end function by function. It
is meant for anyone who wants to learn how a vehicle combat game of its time is put together. The
details, with the program's addresses and layouts, are in the repository's reverse-engineering notes
([`docs/re`](https://github.com/emdzej/openi76/tree/main/docs/re)) and its
[format pages](https://github.com/emdzej/openi76/blob/main/docs/formats/index.md).

## The shape of the program

The game is two programs written in C. `I76.EXE`, about a megabyte, is the game itself: the world,
the cars, the AI, the scripts, the renderer and the sound. `I76SHELL.DLL`, about half that, is the
front end: the main menu, the Trip's screens between missions, the garage, the salvage, the melee's map
and the network lobby. Movies are played by RAD's Smacker library and network play goes through
Activision's own library, Activenet, both as separate DLLs.

The exe is linked from object files in order, so its code falls into contiguous modules. Each one is
a subsystem:

| Module | What it does |
|---|---|
| App | Start-up, the mission's set-up and teardown, the game loop, fatal errors |
| Files and resources | The CD, the file index, the archive, data by name |
| Definitions | The tagged-chunk reader every definition file uses |
| World | The mission file's world: palette, sky, lighting, terrain textures |
| Terrain and roads | The height field, its drawing, the road pieces and bridges |
| Objects and entities | Scene nodes, classes, structures, their damage and destruction |
| Vehicles | Loading a car from its parts, its physics, damage and the wreck |
| Weapons and ordnance | Guns, rockets, mortars, droppers, turrets, the radar |
| Effects | Explosions, smoke, flames, contrails, skid marks, debris |
| AI | Orders, tactics, path search, driving, combat, aiming |
| Script (FSM) | The mission's stack machines and their actions |
| Objectives | The notepad and the map screen |
| Camera and cockpit | The camera modes, the 3D and flat cockpit, the HUD, the mirror |
| Graphics | The display, the 3D pipeline, clipping, the rasterizer, the render queue |
| Sound | The sound manager, engines, the CB radio, the CD music |
| Input | The keyboard, the joystick and mouse, the action map |
| Network | The messages, the players' cars, spawn points, scores |
| Menus | The in-game menus drawn over the last frame |

The program starts by checking that only one copy runs, reads its command line and sets up its
subsystems. It opens a window, finds the CD by its label, builds the file index, and plays the intro and
the credits. Then it loads the shell's DLL and hands it control. The shell runs its own message loop
until the player starts a mission, writes down what to play (the mission's name, the car, the kind of
game) and returns.

The exe then sets up the mission: about sixty initialisers run in order (the clock, the radar, the AI's
teams, the effects' pools, the sound, the input...), then the mission file, the roads, the vehicles,
the script, and the mission's intro movie. The game loop runs while the program's state says "in a
mission". When the state changes (the script's success or failure, the player's Abort or Exit), the
outro plays, about forty shutdown calls undo the set-up, and the program goes back to the shell, starts
the next mission, or exits.

```text
WinMain
 ├─ start-up: window, CD, file index, intro movies
 └─ loop
     ├─ the shell (I76SHELL.DLL): menus, Trip, garage, melee map, lobby
     ├─ mission set-up (~60 initialisers, the mission file, cars, script, intro movie)
     ├─ game loop while in a mission
     │    messages → network → clock → input → AI and script → classes' frames
     │    → weapons → effects → sound → draw → present
     └─ teardown (~40 shutdowns, outro movie) → shell, next mission, or exit
```

Memory is mostly static. The world's tables, the pools of nodes, the render queue's records, the AI's
path search, the ordnance in flight: each is an array of fixed size reserved at start-up. Many are not
bounds-checked. A pool that runs out simply stops giving (the path search gives up, a shot is not
fired). The port keeps the sizes and the limits, and where the original writes past an array's end, the
notes say so.

## Time

The game does not run at a fixed tick. Each pass of the game loop reads the system's millisecond clock,
and the time since the last pass becomes the frame's step, clamped to between a thousandth of a second
and a fifth. A paused game takes steps of zero. Everything that moves is integrated by that step: the
cars, the shots, the smoke, the camera, the AI's timers, the script's clock.

The cars need finer steps than a frame gives. A vehicle splits the frame into sub-steps, about one per
twentieth of a second of the frame, up to twenty, and runs its damage and physics once for each.
A car far from the camera takes one cheap step per frame on a flat plane instead, and is put back on
the ground when the camera comes near. A car's corners against the terrain are swept in a few smaller
steps still, so that a fast car does not pass through a hill.

A variable step means the game is not deterministic in time: two runs with different frame rates
differ. Most randomness comes from the C library's `rand()`. The AI's rolls, the wreck's thrown parts,
the effects' scatter and the sound's pitch all draw from that one generator. With a fixed step (the
port's `--param tick`) and the same input, a run is repeatable, and the port relies on that to test
whole missions unattended.

The script has a clock of its own: the sum of the frame steps after the first few, so that the loading
frame's long step does not count. The mission's timers and its end ("succeed in five seconds") use it.

## The data

Disc 1 holds the program and one big archive, `I76.ZFS`, of nearly four thousand files stored
uncompressed. A small index of the archive's members, read at start-up, turns a name into an offset.
On top of the archive, a resource layer finds files by name and keeps a reference count for each.
Smaller archives (`.pak`) group the art of one screen or one set of textures. The missions are loose
files on the disc beside the archive. Disc 2 holds the movies and the music. See
[the archive](https://github.com/emdzej/openi76/blob/main/docs/formats/zfs.md) and [the index](https://github.com/emdzej/openi76/blob/main/docs/formats/zix.md).

Almost every definition file is the same container: a sequence of chunks, each a four-letter tag, a
size and a payload, which may hold chunks of its own. A reader walks the file with a table of tags and
handlers, so each kind of file is a table and a few functions. A tag the table does not know is read
past. A mission is chunks for the world, the terrain, the roads, the objects, the lines of objects and
the script. A car is chunks for its frame, its specials and its weapons. See
[the container](https://github.com/emdzej/openi76/blob/main/docs/formats/bwd2.md).

A car is assembled from parts by name, which is why the garage can swap them:

```text
car (.vcf) ─┬─ body and its levels of detail (.vdf)
            ├─ paint (.vtf)
            ├─ wheels, by pair (.wdf)
            ├─ engine, suspension, brakes (numbers into a shared component file)
            ├─ armour and chassis points per side
            ├─ specials: nitro, blower, jammer, ...
            └─ weapons on hard points, each a weapon file with its ordnance and effects
```

The formats were made for the game, not for an editor: fixed-size names of thirteen bytes, records
whose layouts are the C structures in memory. The game's own tables (the cars' handling curves, the AI's
combat table, the menus' layouts) are compiled into the exe. The port reads those tables from the
player's copy of the exe at start-up, so neither the data nor the tables are in its source.

## The world

### The terrain

The ground is a height field. The world is a grid of zones, each a square of cells five metres on a
side. A mission's map is 80 by 80 zones in the middle of a much larger, mostly empty grid, so a car
that drives off the edge does not fall out of memory. A cell is sixteen bits: twelve of height in tenths
of a metre, a flag, and three bits of class (dirt, road, water and so on). The class gives the tyres
their grip, the AI its costs and the effects their dust. The `.ter` file is the zones back to back.

The terrain is drawn from a quadtree built each frame around the view. The view's footprint on the
ground becomes a convex outline, nodes outside it are dropped, and a node splits while it is near the
eye for its size. Neighbours of different sizes are stitched by sharing the midpoint of the coarser
edge, so that no cracks open between them. The textures are tiled in two rows of three sizes, picked by
distance.

### The roads and the objects

Roads are pieces laid along splines over the terrain, each with its texture, and bridges are objects
that carry the road. Everything else in the world is an *object*: a structure (a building, a fence, a
gas station), a vehicle, a turret or a helicopter, each a tree of scene nodes with levels of detail.
A mission places its objects one by one or in lines (a row of fence posts is one record).

Each node belongs to a *class*, and a class is a table of functions: start, frame, draw, damage,
collision, remove. The game loop does not know what a car is. It calls the classes' frame functions,
and the car's runs the car's physics. A structure's damage handler knocks out its parts and throws its
chunks; a car's goes through its armour. An *entity* is a node the world tracks: it has a collision
record, takes part in the frame's collision pass, and can be named by the script. Registered callbacks
run when an entity is added or removed, which is how the AI learns of a new car and the network of a new
building.

### Collisions

Each frame a collision pass sets the moving entities' boxes and tests them against each other and
against the structures. Two cars that touch exchange an impulse from their masses and velocities, each
takes damage from the impact's speed, and both make the crash's sound. The ground is a separate test:
the four corners of each car's box are swept against the height field. A shot is a small body of its
own that the same pass tests against the boxes, so a bullet hits whatever box it enters.

## The gameplay

### The cars

A car's data holds its frame (place, velocity, spin), its four wheels with their springs, its engine
with its gears and torque curve, its armour and its parts. The physics of a sub-step goes in order:

1. Each wheel finds the ground under it, with a bump from the terrain's roughness, and its spring
   settles toward that.
2. The car is turned back upright if it lies on its back at rest (the player's car; any other dies).
3. The front wheels turn by the steering.
4. The drive: the engine's torque through the rear wheels' grip, or the brakes on all four; the yaw
   from the steering, limited by the wheels' loads and the ground's grip; a slide when that limit is
   passed; the air's drag, the rolling resistance and the slope.
5. In the air, the car falls and its nose drops; a landing from high enough kills it.
6. The body is set on its wheels again: the plane through the wheels' contacts gives its attitude, and
   the springs lean it in turns and under braking.
7. The engine's revolutions follow the wheels' and the gear.

It is not a rigid-body simulation. The car is a frame that moves in the ground's plane, with the wheels
and springs as a separate layer that tilts it. That is cheaper and more controllable than a full body
and gives the game's heavy, sliding feel. Only crashes and the wreck use a body's impulses and spin.

### Weapons

Weapons sit on hard points. Each has its ordnance: bullets, rockets with their contrails, mortar
shells on a ballistic arc, flames, and what the droppers leave behind: oil, caltrops, mines and blocks. A trigger held fires at
the weapon's rate while it has ammunition. A shot is born at the muzzle with the
car's velocity added, flies under its own rules each frame (straight, guided, falling) and, when the
collision pass finds it in a box, delivers its damage and an explosion's effect. The radar keeps a list
of contacts in range, and a weapon that locks (a guided rocket) takes its target from there. The
jammer hides the player from the AI's radar for a while.

### Damage

Damage is a *packet*: a few amounts with a mask of which ones count, one of them armour-piercing. A hit
on a car first finds the side from the direction of the point hit (front, back, left, right, or from
below). That side's armour absorbs the packet up to its points, then the chassis on that side takes the
rest. What passes both goes to the inner parts on that side (the engine, the brakes, the suspension,
the weapons on that side's hard points) in proportion to the car's health. Specials change the factors:
some armour packs double the armour on two sides.

A car's health is the least of a few ratios: its engine's, its chassis's, its suspension's and so on.
When it reaches zero the car dies. The damage shows in three steps on each side, as the body's textures
swap to the dented and burned frames, and as smoke from the engine that thickens with the damage. A
dying car becomes a wreck for ten seconds: an explosion kicks it up and spins it, and then at random
moments it throws a wheel, a panel or a chunk of debris. The AI's wrecks leave their parts behind as
salvage for the player.

### The AI

The AI drives the same cars through the same controls as the player: throttle, steering, brake, hand
brake and the triggers. It is three layers:

- **Orders** come from the script, once a frame: `goto` a path, `attack` a target, `follow`, `race`,
  `guard`, `evade`, `sit`, `hide`.
- **Tactics** carry an order out. Each vehicle keeps a stack of tactics. An order is a tactic at the
  bottom; each tactic has conditions that push another on top (an obstacle ahead pushes avoiding, being
  stuck pushes backing out, the path found pushes following it) and conditions that pop it. The top
  runs each frame. So an attacking car that hits a wall backs out and then goes back to attacking,
  without the attack knowing.
- **Steering and aim** turn the top tactic's wish (go to this point at this speed) into the controls,
  and decide each frame for each weapon whether to pull the trigger.

To drive somewhere, a car searches a path over the terrain's grid of ten-metre cells, a best-first search
in which a road cell costs a thirtieth of open ground, with the open cells kept in balanced trees. The
search is shared out: one car searches a little each frame, so a crowd of cars that all want a path at
once does not stall the game.

The fight picks a manoeuvre from a table indexed by where the target is (eight sectors around the car),
where the car is from the target's view, the distance band, and the tactic's mode: swing left or right,
pursue, ram, match its course. Each tactic's skill and aggression come from the script.

The firing decision is where skill shows. A gun fires only when a few random rolls fall under the skill
and the shot's closest approach to the target's predicted position is within a tolerance that grows
with the distance. The lead time is blurred by the aim. The target must be in sight. And the shot must
not pass close to a team mate. A poor driver fires less and misses more. A good one leads its shots.

### Missions and scripting

A mission's script is a set of small stack machines compiled from the designers' source. Each machine
has its code (pairs of an operation and an argument), its stack, and its arguments, which are shared
variables. Every frame each machine runs until it yields. An action does something (`teleport`,
`playMovie`, `startTimer`, `successAll`, an AI order) and a condition (`isWithin`, `isShot`,
`isArrived`, `hpLesser`, `isKeypress`) sets the machine's flag for a branch. There are about a hundred
actions. A typical machine is a loop: give an order, check a condition, yield, again.

A mission is assembled from named things the script refers to: the entities (the cars and buildings by
name), the paths (lists of points the AI drives), the sounds and the CB radio's lines, and the
objectives shown in the notepad. A car waiting in ambush is a machine that orders `sit` until the player
comes within fifty metres, or until it is shot, or until another machine sets a shared variable. Then
it switches to `attack`. A mission ends when a machine calls `successAll` or `failAll`, with a delay so
that the last explosion plays out.

The intros are the same machinery: the player's car is handed to the AI with a `goto` along a path,
another machine drives the camera along its own path, and both end on a key press by teleporting the car
to where the mission starts.

### The Trip and the melee

The story is a *Trip* of seventeen missions. The shell sequences it: after a won mission it reads
back the state the exe wrote (the car's damage, its ammunition, the mission's salvage), shows the
salvage to choose from, then the garage to fit the car, then the next mission's briefing. The car
travels between the two programs as two small files of the same chunk format: the car's make-up and its
state. A *bookmark* saves the Trip's place and the car's files under a name.

The melee is the same game without a story: a map, a car, and waves of AI cars or other players. The
script's machines step aside and every AI car hunts the player. The spawns, the waves and the standings
are the melee's own code.

### Network play

A network game is a few peers in an Activenet session (the host sets the limit), one of them the *keeper* whose clock the
others follow. Each machine owns its player's car: it simulates it, sends its state two to five times a
second (more often on a faster link), and draws the others' cars from their messages, extrapolating them between messages. A player's
triggers and locks travel too, so the other machines fire the same shots from the remote car and see
them hit. Damage is applied only to the car a machine owns: a hit on a remote car is drawn but does
nothing there, and the victim's machine counts the hit on its own copy and announces the death and
its killer. Buildings destroyed on one machine are sent as a bitmap of the mission's objects, and the
others destroy theirs. Respawning asks the keeper for a free spawn point. Chat lines, name tags and the
scores are separate small messages.

## Saving

There is no save in the middle of a mission. A mission is replayed from its start. The only state kept
between missions is the Trip's: the car's two files, the bookmark records and the list of what the
garage holds. A melee and a network game keep only their standings.

## Presentation

### The renderer

The renderer draws in eight bits, 640 × 480 (or a smaller mode), entirely in software. Colour comes
from a 256-entry palette per mission, and the palette's lighting comes from tables. A *luminance*
table maps a colour and a light level to the nearest palette entry, so shading a texture is one table
lookup per pixel. A 64 KB *translucency* table maps a pair of colours to their blend, which gives the
smoke, the glass and the headlamps' beams.

Everything in a frame goes into one queue of screen-space polygons, sorted into depth buckets and drawn
far to near: the painter's algorithm, with no depth buffer. The terrain, the roads, the objects, the
cars, the effects, the shadows and the tracers all queue their polygons. The sky is drawn straight onto
the screen before the queue is flushed, and the cockpit and the HUD after it. The rasterizer has a
filler for each kind of polygon: flat, Gouraud, affine and perspective-correct textures, with shaded,
translucent and shadow variants. The polygons are clipped in 3D against the view and projected in
the FPU's single precision. That rounding is part of the look, so the port reproduces it.

Lighting is per vertex from the sun or the moon of the mission's hour, plus the dynamic lights at night
(the headlamps, the explosions). Shadows are drawn into a mask, then darken the scene.

### The cockpit and the HUD

The 3D cockpit is the player's own car's model at its innermost level of detail: the interior, with
each gauge a part of its own. The instruments are textures the cockpit rewrites each frame: the
speedometer's needle, the tachometer, the damage diagram, the weapons' and specials' panels, the
radar's display. The flat cockpit draws the same instruments as 2D pictures over the view. The HUD
adds the target's box and the kill messages, and the rear-view mirror is a second, small view of the
world drawn into the dashboard.

### Sound and music

Each sound file carries its own priority, its volume, how many copies may play at once and whether it
loops. The sound manager admits a sound only when a voice is free or the new one outranks a playing one,
so in a big fight the guns and the speech keep their voices and the tyres give theirs up. A sound has a
place in the world and its volume and pan follow the camera. The engines, the horns, the siren and the
tyres are continuous sounds driven by the car's state. The CB radio queues its lines so that they don't
talk over each other. The music is disc 2's CD-audio tracks, played by the drive itself. Each mission names its first track, and
the tracks then play on one into the next.

### Movies and menus

The intros, the outros and the story's cut-scenes are Smacker movies on disc 2. The in-game menus are
drawn over the last frame of the mission, as diner checks written in a handwriting font, and the shell's
screens are pictures with hot spots for the mouse.

## Lessons for building such a game

- **Make every system a table of functions.** The classes' handler tables and the chunk reader's tag
  tables let a small loop drive a world of very different objects and files, and let a new kind be added
  without touching the loop.
- **Split the AI into orders, tactics and controls.** A stack of tactics with push and pop conditions
  makes a car recover from a wall or a jam on its own and then resume what it was doing. Driving through
  the player's controls keeps the AI honest and lets the same code drive the player's car in a cut-scene.
- **Let skill be probability.** The AI's skill is a few random rolls and a blurred lead time, not a
  different behaviour. One parameter in the script then spans from a hopeless thug to a sharpshooter.
- **Spread expensive work over frames.** The path search runs one car at a time, a little each frame. A
  far car takes one cheap step instead of twenty.
- **Script missions with many small machines.** Each car or event gets a short loop that waits for a
  condition and gives an order. Shared variables make them cooperate, and the designers can add an
  ambush without touching the engine.
- **Assemble content from named parts.** A car is a list of parts by name, and so is a mission. The garage,
  the salvage and the carry-over between missions all come out of that one decision.
- **In a peer-to-peer game, let each machine own its car.** Simulating your own car and trusting the
  victim's machine with the damage keeps the game consistent without a server, at the price of trusting
  the peers.

## Further reading

- The [format pages](https://github.com/emdzej/openi76/blob/main/docs/formats/index.md): the container,
  the archive and its index, the cars, the textures, the movies.
- The reverse-engineering notes in the repository's
  [`docs/re`](https://github.com/emdzej/openi76/tree/main/docs/re), one page per subsystem, with the
  addresses and layouts this page leaves out: `architecture.md` for the program's map, `vehicles.md`,
  `ai.md`, `fsm.md`, `weapons.md`, `render.md`, `network.md`.
- [How Oni works](https://openoni.emdzej.pl/guide/theory), the same kind of guide for another port.
