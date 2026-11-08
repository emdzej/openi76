# Movies (ported)

`src/movie.c` ports the exe's side, 0x44e6b0-0x44fc00. `src/smacker.c` is the port's `SMACKW32.DLL`
(docs/formats/smk.md), with only the calls the exe makes. The exe imports these by ordinal:

| Ordinal | Name | Use |
|---|---|---|
| 14 | SmackOpen | the movie |
| 18 | SmackClose | |
| 19 | SmackDoFrame | the frame decoded |
| 21 | SmackNextFrame | |
| 32 | SmackWait | |
| 23 | SmackToBuffer | where the frame goes |
| 38 | SmackSoundUseDirectSound | |
| 17 | SmackSoundOnOff | |
| 2, 3, 5, 6, 7 | SmackBufferOpen, Blit, NewPalette, Close, SetPalette | the windowed path |
| 25, 28 | SmackColorRemap, SmackToBufferRect | the windowed path |

## Where they play

- **At start-up**, WinMain: Sound_Init, then 0x44e6b0 (SmackSoundUseDirectSound on the game's DirectSound),
  then `introf01.smk` and `credf01.smk` (0x4a8364, 0x4a8358). Each plays to its end, or until Esc or a
  click.
- **Before a mission**, WinMain plays the movie the shell chose (0x58dc90, its mission's `INTnnF01`). The
  shell is not ported, so this is not reached yet.
- **The script's `playMovie`** (0x40d0d0 action 51) plays a name from the mission's sounds table. The
  mission's loop then shows it as it would a pause.

A pass of WinMain's loop while the play's state (0x4db008) has a bit of 0x3e set runs 0x44fab0 (the pause's
and the map's keys). Then, in state 0x20, it shows the movie's next frame (0x44ed90); otherwise it runs
Sound_Frame(1). The clock's tick and the input's update run on every pass.

## Opening (0x44e6e0)

1. Returns 0 if a movie or its buffer is open.
2. Pauses the music (0x468a50) and empties the mouse's and keys' messages.
3. Sets the primary buffer to 22,050 Hz (0x477ff0).
4. Builds the path: the CD's drive letter (0x484bc0) and `:\cutscene\`, or `smk\` without a CD.
5. Opens the movie (flags 0x60, with 0x2000 or 0x4000 by the file's attributes from 0x44f2b0; buffer
   0xfa000). If that fails, it retries at the other path.
6. Switches the display mode to 5 (640 × 480), or 9 for a movie 320 wide (none is).
7. Opens a SmackBuffer for the windowed path.
8. Sets the state to 0x20 (0x44f7d0), pauses the game's sounds (0x4688d0), and points the movie at the
   buffer.

A failure on the way restores the mode and the rate.

0x44f7d0 sets the state to its argument, or to 1 if the state is that already.

## A frame (0x44ed90)

1. Returns 1 while SmackWait says the frame is not due.
2. With the full-screen flag 0x4d66e0 set (the exe's default), and the window focused:
   - locks the surface, decodes the frame onto it (SmackToBuffer, SmackDoFrame), unlocks it and flips;
   - if the frame has a new palette (the movie's +0x68), takes its 256 colours (+0x6c) through the gamma
     (0x43c170) into the hardware palette (PC_NOCOLLAPSE). 0x4dafb4 asks for it to be set again after a pause.
3. Without the flag, 0x44e400 does the same through the SmackBuffer, its remap and its blits.
4. At the last frame (frames - the current one == 1), it closes:
   - the buffer and the movie;
   - the state back to 1;
   - the display mode back;
   - the screen cleared to black (0x437d90), flipped, and the game's palette (0x4d613c) set again;
   - the rate back to 11,025 Hz, twice;
   - the sounds resumed (0x4688e0), and the music (0x468a40);
   - the messages emptied.
5. Otherwise it calls SmackNextFrame.

## Closing and the keys

- **0x44f090** (WinMain's, at a mission's end) closes the same way, without the resume.
- **0x44f170** closes with the resume. The window's procedure (0x402c9e) calls it:
  - on a key down mapped (0x4e8e90) to Esc (0x1b);
  - on a left click, in state 0x20.
- **The pause's key** (mapped to 0x1ff) toggles the pause (0x44fc00) in states 2 and 0x20. In other
  states, the key messages go to 0x480f60.
- **0x44fc00, the pause**:
  - Off to on: stops the movie's sound (0x44f570, SmackSoundOnOff 0; 1 if a movie is open). Without one,
    and with the previous state (0x4db00c) 1, it pauses the sounds and the music. Then it saves the state
    and sets 2.
  - On to off: 0x44f5a0 asks for the palette again and turns the sound on. Without a movie, it resumes the
    sounds. The state goes back to the saved one, or to 1 if it is that already.
  - The game's PAUSE_GAME key (0x2c) calls it from the menus' poll (0x480720) and from the typed keys'
    commands (0x4809b0).

## The port

- The frame is decoded into the decoder's own 8-bit picture. That picture is copied to the screen and
  shown with the movie's palette, gamma corrected.
- The track goes into a DirectSound buffer of the port's own: a looping ring of four seconds, written as the
  frames are decoded. The track's first chunk holds about a second ahead.
- The gamma's table (0x43c1e0) is built at the app's start, as WinMain builds it before the intro; the
  movies' palettes go through it.
- Each movie's open and close is logged (the port's lines, with the frame it closed at).
- SmackWait's clock starts at the first frame; with `--param tick` it is the fake clock.
- `--param intro=0` (the port's) leaves out the start-up movies. Headless runs that are not about them use
  it.
- Checked in a headless gasm run: frame 300 of the intro is identical to ffmpeg's; Esc closes the intro and
  then the credits (at their frames 25 and 38), after which the loading screen shows, identical to
  ffmpeg's decode of `LOADSCR.PCX`.
- **Not ported:** the music's pause and resume (CD audio, docs/re/port-plan.md), the windowed path
  (0x44e400), WM_QUERYNEWPALETTE (0x44ed60), and the shell's movie before a mission.
