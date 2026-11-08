# Audio (the sound manager ported, diffed)

`src/sound.c` (the game's sound manager, 0x466ab0-0x468970 and 0x477600-0x478140) on `src/dsound.c` (the
port's DirectSound). `tools/emu/diff_objects.py --call sound` runs the original's manager against a fake
DirectSound that logs every call. The port's `dsound.c` logs the same lines, and the two logs must be
identical frame by frame. `--call engine` does the same for the engines, horns and siren, and `--call
radio` for the CB radio. The music is `src/music.c` on `src/cdaudio.c` (below). Not ported yet:
- the shell's sounds (0x467390, 0x4674a0, 0x4674f0);
- the poetry's and the cheats' CB calls (0x4513c0, 0x408d30).

## The files

A sound is asked for by a `.wav` name. 0x478080 drops the directory and the extension and tries
`<name>.gpw`; when that is not in the resources it uses the name itself. Disc 1 has 114 `.gpw` files, so
most names that the code builds and that have no `.gpw` (vmgun, vvcoll, vmissile, vcsign in v1.0) play
nothing. A `.gpw` is a 0x1c-byte `GAS0` header of seven words, then a RIFF WAVE:

| Word | Field | Default (0x467510) |
|---|---|---|
| 0 | `GAS0` | |
| 1 | priority | 50 |
| 2 | copies of the name that may play at once | 1 |
| 3 | loop: 0 once, 1 looping, 2 looping while asked again each frame | 0 |
| 4 | volume, 0-100 | 100 |
| 5 | random pitch | 0 |
| 6 | the rate (the wave's) | |

-1 is "the default". On disc 1 the weapons' sounds have priority 70, the explosions' 60, the speech 75.
No file asks for the random pitch (one of ten factors 0.8 to 1.16, 0x4dc748). The files come through
the resource layer's cache (0x492420: 500,000 bytes, ten entries; the port's is the resource layer's own).

## The records

A record is 0x7c bytes (`Snd`):

| Offset | Field |
|---|---|
| +0 | next |
| +4 | name (16 bytes), the start of the caller's 0x2c-byte part |
| +0x14 | flags: 2 an effect, 4 speech, 1 kept through the pause, 8 played on in it, 0x10 starts in a pause, 0x20/0x40/0x100 the pitch, the volume, the place changed, 0x200 placed once |
| +0x18 | frequency (float, 11,025) |
| +0x1c | volume (float, 100) |
| +0x20 | pan (float) |
| +0x24, +0x28, +0x2c | hooks: at the start (1), each frame (2), at the end (3) |
| +0x30 | the GAS0 header (seven words) |
| +0x4c | the file (the caller's data with state 0x200) |
| +0x50 | its wave, then its samples |
| +0x54 | IDirectSoundBuffer |
| +0x58 | IDirectSound3DBuffer |
| +0x5c | the node, referenced ("aud") |
| +0x60 | times sounding |
| +0x64 | a copy (its buffer duplicated from a loaded one) |
| +0x6c | copies made of it |
| +0x70 | bytes |
| +0x74 | asked again this frame (loop 2; 0xff fresh) |
| +0x77 | the header's priority as a byte |
| +0x78 | state: 1 on the playing list, 2 parked, 4 sounding, 8 paused, 0x10 left its node, why parked: 0x20 bumped, 0x40 out of range, 0x80 muted, 0x100 replaced; 0x200 the caller's data |

The manager (0x5e2ec0, nine words): the DirectSound object, the primary buffer, the listener, the playing
list's count, the voices (at most that many play), the effects' and the speech's volumes (0-10), the
playing list and the parked list. Both lists are sorted by priority, highest first. A record goes after
the first of its equals and before the first lower one (0x467c10).

## Playing (0x467260)

`Sound_Play(name, node, params, header)`:
1. Nothing without the manager or a name. During a menu or a pause (0x4db008 & 0x3e) a sound only plays
   with flag 0x10, or in network play.
2. With a node and a header not looping (word 3 not 1), nothing beyond 600 m of the listener's entity.
3. A held sound (loop 2) already playing at that node is only marked as asked again.
4. At the user's vehicle (its root flagged 0x10) the priority is raised by 20. The caller's header is
   changed in place.
5. A record is made (0x477f00): the params copied, the name over them, flag 2 (an effect) unless 2 or 4
   was given. It is then loaded (0x477690).

The wrappers: 0x468580 at a node (none: the user's), 0x4685d0 held (loop 2), 0x468640 placed once (flag
0x200: the 3D minimum distance 100 and the node let go after the first placing). 0x4765d0 plays a sound
flat when its node is the user's. 0x4683c0 maps the vehicles' event numbers to names:
- 4 landing (the user's);
- 5, 6, 7 the collisions and the explosion, placed;
- 8, 9 the guns;
- 10 the user's skid, looping, and 11 its end;
- 16 the CB sign;
- 1-3 go to the engine's (below).

**Loading (0x477690).** A record whose name has a loaded record with its own buffer (0x467830, the
playing list first) becomes a copy:
- it takes that record's header and file;
- the caller's header is merged in (0x467580: its priority added, its other set words taken);
- its buffer is duplicated (DuplicateSoundBuffer);
- the loaded one counts the copy.

Otherwise the file is read, its header taken with the defaults and merged. Then the admission
(0x467d80), tried again while it answers 6:

| Answer | When |
|---|---|
| 5 | its kind (effect or speech) has volume 0 |
| 3 | its node is beyond 600 m |
| 1 | fewer copies of the name play than allowed. When the voices are full and a lower priority plays, the playing list's last record is parked as bumped. |
| 4 | the voices are full and nothing lower plays |
| 6 | too many copies, and one of them (compared with case) has a lower priority: that one was stopped and parked as replaced |
| 2 | otherwise |

For 1, 3, 4 and 5 the buffer is made:
- a copy is duplicated;
- a new one is CreateSoundBuffer'd with flags 0xb2 (3D) at a node, else 0xe2 (a pan), then filled through
  Lock and Unlock;
- the rate goes into word 6 and the frequency;
- at a node the 3D interface is queried;
- the file is released (the cache keeps it).

Then 1 puts it on the playing list, and 4, 3, 5 park it (0x468280) as bumped, out of range, muted. 2, or a
failure, frees it.

**Parking (0x468280)** stops the buffer and moves the record to the parked list with the reason:
- a sound played once gets no reason, so it is never readmitted;
- a looping one keeps its node;
- a copy that does not loop is freed instead.

On the parked list a record with the same name and node already parked takes its place. Its node is let
go unless it loops.

## The engines, horns and siren (0x4761b0-0x476580)

**The engine's loop (0x476240)** starts with the vehicle's class (0x4761b0 from the physics' set-up and
class 9's start) and again with event 1 when the engine starts. Its name comes from the vehicle's kind
(+0x47c):
- 6 eitank.wav;
- 5 eimarx.wav;
- 7 aheli.wav;
- otherwise the engine's kind (its +0x28): einp1, eihp, eishp, eisv.

The loop has priority 79 at the user's vehicle and flags 9 (kept and played on through the pause). The
record goes into the vehicle data's +0x10c.

**The pitch hook (0x4761d0)** runs for every kind but the tank and the helicopter, at the start and each
frame. It sets the frequency to (revolutions - 1,050) / 4,950 × 8,268.75 + 8,268.75, so 8,268.75 Hz at
idle, and marks the pitch, volume and place changed. It also copies the engine's +8 into the record's
+0x76, which nothing reads. A record whose node has lost its data is freed by the hook, and the frame
then still uses it: the port frees it in the frame instead.

**Event 2 (0x4763b0)** frees the loop when it is on the playing list. A parked loop, out of range or
bumped, stays, a quirk.

**The horns (0x476380)** are vhorn1-6.wav by the vehicle's kind; kind 0 has none.
- 0x476440 sounds the horn once (event 3, and the AI's avoidance one time in four with something ahead).
  It loops at a death (Vehicle_Damage 0x429eb0): the dead driver on the horn.
- 0x476420 is the user's horn key, held: loop 2 at the user's vehicle.
- 0x476480 frees it when the wreck is hidden (0x42b830, with the engine's 0x4763b0).

**The tyres (0x475ee0, each frame of the user's vehicle on the ground)** take one of three sounds by the
terrain's class (0x4a66e8, eight rows), each held only while asked again:
1. The skid (tskid1-4) while sliding (+0x454 bit 2).
2. Above 7 m/s, without a skid:
   - the turn's (tturn1-4) with bit 0x2000;
   - else the surface's (vcddirt, vcdsand, vcdgrav; none on roads).
3. Also above 7 m/s:
   - tflat.wav while a wheel's +0x44 is set;
   - fdmg1.wav braking (+0xe4 below -0.5) on brakes worn below half.

**The siren (0x4763e0)** is aps2.wav at a node, on or off, from the script's setSiren.

**The starter (0x4764b0)** is the user's, flat:
- esnp1 above 0.66 of the engine's +0 / +4;
- esnp2 from 0.33 (a compare of the float's bits with 0x3ea8f5c4);
- esnp3 below.

Each table has one 0x14-byte entry per engine kind: a name, then the time at +0x10. 0x476510 plays it
when the engine is asked to start and gives that time to the vehicle's +0x450. 0x476580 frees it when the
engine runs.

## The CB radio (0x46fa50-0x46fdb0, `src/radio.c`)

The spoken messages wait in one queue (0x4dd904) of 0x24-byte records:

| Offset | Field |
|---|---|
| +0 | the sound's name (16 bytes) |
| +0x10 | the speaker's node |
| +0x14 | its sound record |
| +0x18 | the kind |
| +0x1c | GetTickCount when asked |
| +0x20 | next |

Only the head plays. Its sound is speech (flag 4) kept through the pause (1), flat, at priority 10,000. Its
end hook (0x46fcb0) drops it and plays the next whose speaker is not dead (0x200). A message that fails to
play is dropped for the next.

**Asking (0x46fa70)** goes through these tests in order:
1. Nothing while muted (0x4dd908). "STOPCBXX" mutes the radio until WinMain's next mission turns it on
   (0x46fa50).
2. Nothing from a dead speaker.
3. With the speech or the voices setting under 2, it answers true and queues nothing (so the checks' runs,
   whose settings are 0, never queue).
4. On a queue:
   - kind 5 is refused;
   - kind 6 is refused behind a message of kind below 5;
   - kind 1, or a kind below 5 behind one of 5 or more, empties the queue first.
5. Kind 2 cuts the playing message and plays at once, the rest after it. Any other kind goes behind the
   last and answers false.
6. On an empty queue it plays at once and answers whether it plays.

Kind 4 is dropped when it starts more than 10 s after it was asked. A message from the user's vehicle calls
the handset up while it plays, unless the dashboard or the handset already is. Dropping one of the user's
lets the handset go.

**The script's actions** take a sound's index into the mission's sounds:
- cb: kind 3, only when the file is there;
- cbFrom: kind 3, from an entity;
- cbPrior: a kind;
- cbFromPrior: an entity and a kind;
- killCB empties the queue;
- isCBEmpty is the scripts' wait for a message to be over.

**At a vehicle's death or wreck (0x46fdb0)** its waiting messages are dropped. When its message is the one
playing, cmike.wav (kind 2) cuts it. The manager empties the queue:
- when everything stops (0x466de0);
- when the voices are cut below a playing message (0x467100).

### The poetry (0x451320-0x4513c0)

Taurus recites poems on the `poetry` key (C).

- At each mission's start (0x451320) two lists are made and shuffled: the 15 poems POSTA01-15.WAV
  (0x4db150, names 0xd bytes apart) and the user's 5 answers GDSGC42-46.WAV (0x4db228).
- The shuffle is the CRT's qsort with a comparison of ftol(rand / 32767 × 2 − 1) (0x4512f0). That is 0
  except at rand's two ends, so qsort's own way with equal elements decides the order. crt_qsort
  reproduces it.
- WinMain then marks t04 and t06 as kind 1 and t05 as kind 2 (0x4513a0).
- The key (0x4513c0) does nothing unless the CB queue is empty. Otherwise the user asks (GDSGC40.WAV,
  kind 5). If that plays, Taurus (the script's actor `taurus`, 0x409b10) says the next poem and the user
  answers with the next answer, both kind 6. A list that has been gone through is shuffled again.
- In a kind 1 mission the user's GDSGC41.WAV follows the asking instead. In a kind 2 mission nothing
  happens.

## The frame (0x466b70)

WinMain calls it each frame in a mission with 1, after the camera's step, then 0x466dd0(1). Without the
manager nothing happens. With 1:
1. **Readmission (0x468010).** While there is room, the parked records that loop and were bumped, out of
   range, replaced, or muted with their volume now on are admitted again. Any answer but 0 puts them on
   the playing list, even out of range (a quirk; the range check parks it again).
2. **The range (0x4680d0)** parks the playing records whose node is beyond 600 m of the listener's entity
   (the camera's, else the user's). The parked ones out of range that loop and are back within it are
   reloaded.
3. **The listener (0x467a60)** is put at the view's eye, along the camera's z axis (front) and y axis
   (top), with the entity's velocity. It is deferred.

Then each playing record:
- Its node freed: the record is freed. Paused: skipped.
- **Not sounding yet: it is started (0x467610).** The volume and the rate come from the header (with the
  random pitch, one of the ten factors). The start hook runs. Every change is marked and made, then it is
  played, looping for loops 1 and 2.
- **Held (loop 2)** and not asked again this frame: it is stopped.
- **Ended** (the buffer no longer playing): parked (a looping or own buffer) or freed (a copy), unless its
  end hook keeps it.
- Otherwise it is brought up to date. It is marked to be placed when it has a node. The volume is taken
  again from the header, and the frequency from the rate when the pitch is not random. Then the frame hook
  runs and the changes are made (0x466ea0):
  - the frequency;
  - the volume, as the table entry of volume × 0.1 × the kind's setting (0-10);
  - the pan for a flat sound;
  - the 3D place at a node (0x466fa0). That is the node's world position and its class's velocity, or
    the listener's own for the user's vehicle in the cockpit. The minimum distance is 10 (100 placed
    once), the maximum 400, deferred. A placed-once sound then lets its node go.

The deferred settings are committed when anything was updated. Last, while the files' bytes exceed the
cache (500,000 here: the caps' word is 0 without hardware), the parked records at no node and not looping
are freed, fewest copies first (0x467b70).

**The volume table (0x467170)** has 101 entries in hundredths of a decibel:
- entry 0 is -10,000 (silent);
- entry i is 4 × (100 / ln 2) × ln(i / 100), so 50 gives -400 and 1 gives -2,658.

The settings' bytes (0x5e14de-0x5e14e1, 0x44d660's defaults) are:
- the music, 3;
- the effects, 5;
- the speech, 10;
- the voices, 8.

A volume of 1 counts as off.

**The set-up (0x466ab0)**:
1. DirectSound, exclusive (0x477c10).
2. With the 3D on (0x4dc778, set in v1.0), the primary buffer with CTRL3D. Its caps give the cache's size
   (500,000 when 0). Its format is 11,025 Hz, 8 bits, stereo (0x477ff0).
3. The listener.
4. The 3D defaults (0x467f70): distance and rolloff factors 1, Doppler 0.8, cones of 360 degrees, minimum
   distance 25, maximum 400.
5. The voices (0x467100), the volume table, the volumes, the music's set-up.

The end of a mission stops and parks the playing (0x468380), stops and frees everything (0x466de0) and
frees the parked (0x468340).

**The pause (0x4688d0, 0x4688e0).** Outside network play each playing record that is not marked to play on
(flag 8), or every record in state 0x20, is stopped. It is then kept paused (flag 1), parked, or freed. The
resume restarts those not sounding.

## The music (0x4686b0-0x468830, 0x4782b0-0x478a90, `src/music.c`) — confirmed

The music is disc 2's audio tracks, played by the CD drive through MCI's `cdaudio` device. The drive's
analogue output goes to the sound card's CD line, whose volume is set with `auxSetVolume`. The exe never
reads the samples itself.

**The drive (0x4782b0-0x478a90).**
- The open (0x4782b0) needs disc 2 in the drive (0x484cf0, the label `I76_CD2`). It then opens the
  device, sets the time format to TMSF (track, minutes, seconds, frames, one byte each) and finds the
  aux device of the CD line (0x478200).
- The mode (0x478590): 1 open (no disc), 2 stopped, 3 playing, 4 paused, 5 no device. A failing status
  closes the device.
- The tracks (0x478900): at most 50, each one's TMSF position, then a last entry, the count + 1.
- A track t plays to the start of track 15, or to t + 1 from 15 on (0x4787f0). Tracks 2-14 thus play to
  the end of the disc's mission music, without stopping between them.
- Pause and resume share one function (0x478650): from stopped it plays, from playing it notes the
  position and pauses, from paused it resumes.
- The volume (0x478a90): the music's level (1 counts as 0) times 0xccc, at most 0xffff, on both channels.

**The game's side (0x4686b0-0x468830).**
- A mission's track is the WRLD chunk's dword at +8 (0x4686b0, from the world's load).
- Play (0x4686c0) closes and reopens the drive each time. The track is kept unless -1 is given. Without
  the track on the disc, it is forgotten (-1). The track plays only when forced or when the music's
  level is above 1. It notes whether this is the shell (state 6).
- Stop (0x468800), pause (0x468780) and resume (0x4687c0). The pause and resume act on the drive only
  when the music's level is above 1.
- The frame (0x468830, from the sound frame) looks every 60 s, or 5 s in the shell (GetTickCount). With
  the drive open or gone, the music is off. When the mode changed (the play ran out), the shell plays
  its track again and a mission plays from track 2.

**Who calls it:**
- WinMain stops the music before a mission's first movie and plays the mission's track after it; a
  won mission stops it before the last movie; a Replay stops it and plays it again.
- The shell (through the exe's table, +0x54 and +0x50) stops it and plays track 15, forced.
- The game menu (0x44b8f0) plays it forced (so a music level of 1 can be heard while it is set); its
  closer (0x44b9c0) stops it when the level is below 2, and in a mission plays it again.
- The pause key's pause and resume (0x4688d0, 0x4688e0) pause the effects and then the music.

**The port's drive (`src/cdaudio.c`).** The gasm runner passes disc 2 as the asset `cd2`. The BIN image
(`I76_CD2.BIN`) holds the audio tracks as 2352-byte sectors of 44.1 kHz 16-bit stereo PCM, and its CUE
sheet, passed as `cd2cue`, gives each track's start (INDEX 01). The MCI calls are answered from those, and
the playing sectors are read and added into the output on the audio thread, through the aux volume.
Without a CUE, or with the data track's ISO alone, the drive has no audio tracks and the game plays
none, as with a disc without them:

    gasm-run openi76.wasm --headless 600 --asset cd=I76_CD1.ISO \
        --asset cd2=I76_CD2.BIN --asset cd2cue=I76_CD2.CUE

## The port's DirectSound

DirectSound is not in the exe, so how it is heard is the port's (`src/dsound.c`).
- **Buffers** are PCM in memory. The mix is made at the primary format (11,025 Hz, 8 bits) and brought to
  the host's 44,100 Hz.
- **Volume and pan** are in hundredths of a decibel.
- **3D buffers**:
  - loudness is the inverse distance from the minimum distance, held past the maximum;
  - the side comes from the listener's right axis (top × front), as at most 0.8 off the far side;
  - Doppler is (343 + f · v_listener) / (343 + f · v_source) along the line between them, within 0.5-2.
- **Deferred settings** take effect at the commit.
