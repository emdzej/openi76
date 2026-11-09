---
layout: home

hero:
  name: OpenI76
  text: Interstate '76, reimplemented
  image:
    src: /hero.svg
    alt: "A desert road at sunset running to the horizon between mesas, its centre line rushing towards the viewer"
  tagline: "A faithful reimplementation of Interstate '76 (Activision, 1997) in C11, ported function by function from the Windows game. It runs the game from your own discs, as one WebAssembly module for the gasm runtime, natively and in the browser. A work in progress: the missions run, most are still to be played through."
  actions:
    - theme: brand
      text: Play in the browser
      link: /play/
      target: _self
    - theme: alt
      text: Get started
      link: /guide/
    - theme: alt
      text: Status
      link: /guide/status

features:
  - title: Faithful
    details: Not an emulator and not a remake. The goal is the original's behaviour, its physics, AI, mission scripts and quirks included, ported from I76.EXE and checked against it in an x86 emulator.
  - title: Your own data
    details: Reads your discs or their images. Even the tables inside the original program are read from your copy at run time. Nothing from the game is in OpenI76.
    link: /guide/game-data
    linkText: Game data
  - title: Runs on gasm
    details: One openi76.wasm for macOS, Linux, Windows and the browser on the gasm WebAssembly game runtime, drawing the original's 8-bit software renderer.
    link: /guide/gasm
    linkText: Running on gasm
  - title: The whole car
    details: The cockpit with its gauges, radar and damage display, the weapons and specials, the CB radio, the music from disc 2's audio tracks.
    link: /guide/status
    linkText: Status
  - title: Melee and network play
    details: The melee's arenas, instant melee and network play, hosting and joining through the shell's own screens over a relay.
    link: /guide/status
    linkText: Status
  - title: Enhancements, opt-in
    details: The original by default. Switch on the GL renderer for the 3D at your screen's resolution, with smooth textures and a depth buffer, the same game underneath.
    link: /guide/enhancements
    linkText: Enhancements
---

<div class="vp-doc" style="max-width: 1152px; margin: 48px auto 0; padding: 0 24px;">

## Screenshots

Renders of OpenI76 itself: headless runs of `openi76.wasm` on the game's discs at the original's
640 x 480, scaled up.

<div class="shots">
  <figure><img src="/screenshots/01-main-menu.jpg" alt="The main menu with the game's logo, Trip, Melee, Options and Exit" loading="lazy"><figcaption>The shell's main menu</figcaption></figure>
  <figure><img src="/screenshots/02-t01-chase.jpg" alt="The orange car from behind on desert ground beside a road, in the chase camera" loading="lazy"><figcaption>The first mission in the chase camera</figcaption></figure>
  <figure><img src="/screenshots/03-t01-cockpit.jpg" alt="The cockpit: radar, weapons panel, damage diagram and mirror, a car ahead on the road" loading="lazy"><figcaption>The cockpit, following Taurus</figcaption></figure>
  <figure><img src="/screenshots/04-t01-pursuit.jpg" alt="The cockpit on the highway, chasing a car" loading="lazy"><figcaption>A pursuit on the highway</figcaption></figure>
  <figure><img src="/screenshots/05-t01-base.jpg" alt="The cockpit facing a compound on the horizon" loading="lazy"><figcaption>Toward the objective</figcaption></figure>
  <figure><img src="/screenshots/06-t03-canyon.jpg" alt="The car at dusk in a canyon by a winding road" loading="lazy"><figcaption>A canyon at dusk</figcaption></figure>
  <figure><img src="/screenshots/07-a01-training.jpg" alt="The car on a dirt road through the desert with cacti" loading="lazy"><figcaption>Training</figcaption></figure>
  <figure><img src="/screenshots/08-s07-desert.jpg" alt="The car on open desert under a teal sky" loading="lazy"><figcaption>Open desert</figcaption></figure>
</div>

## The GL renderer

The same moment of the first mission drawn by the original's software renderer (left, at its 640 x 480)
and by OpenI76's opt-in GL renderer (right, smooth textures and a depth buffer). More in
[Enhancements](/guide/enhancements).

<div class="shots pair">
  <figure><img src="/screenshots/gl/f3000-original.jpg" alt="The cockpit view in the original renderer, blocky textures" loading="lazy"><figcaption>The original</figcaption></figure>
  <figure><img src="/screenshots/gl/f3000-gl.jpg" alt="The same cockpit view in the GL renderer, smooth and sharp" loading="lazy"><figcaption>The GL renderer</figcaption></figure>
</div>

## Built for gasm

<p><a class="gasm-badge" href="https://gasm.emdzej.pl"><img class="gasm-badge-light" src="https://gasm.emdzej.pl/badge/built-for-gasm-light.svg" alt="Built for gasm" width="120" height="44"><img class="gasm-badge-dark" src="https://gasm.emdzej.pl/badge/built-for-gasm-dark.svg" alt="Built for gasm" width="120" height="44"></a></p>

OpenI76 is one WebAssembly module, `openi76.wasm`, running on [gasm](https://gasm.emdzej.pl), a
portable game runtime: the same module natively (`gasm-run`) and in the browser. See
[Running on gasm](/guide/gasm).

## Status

The game runs from your discs or their images: the intro, the shell's menus, the trip with its garage,
the training and the melee's set-up, and every mission's terrain, roads, objects, vehicles and script. In a
mission the vehicles drive and collide, the AI drives and fights, every weapon flies and hits, and the
cockpit, the radar, the sounds and the music run. The first four missions of the trip play to a win; the
others are being played through, and network play runs over the gasm relay. Details in [Status](/guide/status).

## Sibling projects

OpenI76 follows the conventions of [OpenCMR2](https://opencmr2.emdzej.pl) (Colin McRae Rally 2.0) and
[OpenRF](https://openrf.emdzej.pl) (Return Fire): a portable C11 core, ported from the original
executable, on gasm.

</div>
