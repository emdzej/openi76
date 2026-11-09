# Enhancements

OpenI76's default is the original: its 8-bit software renderer at 640 x 480, pixel for pixel. What the
port adds comes on top of that and is always opt-in. None of it changes the game: the same code runs the
same simulation, so a mission plays the same with or without it.

## The GL renderer

The original drew every polygon itself, into an 8-bit frame, through the palette's lighting and
translucency tables. The GL renderer keeps all of that running and draws the same polygons a second time
with your graphics card, at a higher resolution and, if you want, with smooth textures and a depth
buffer. It shows GL's pixel wherever the polygon was the last thing drawn there, and the original's
everywhere else, so the 2D art (the menus, the dashboard's bitmaps, the text, the sprites) stays as it
was, at 640 x 480. The lighting, the translucent smoke and fire, the shadows and the recoloured polygons
go through the same tables as the original's, in a shader.

It is still being built. It is a second module, `openi76-gl.wasm`, beside the default `openi76.wasm`.

### Turning it on

- **In the browser**: on the [player](/play/), tick **GL renderer** in the Renderer card before you press
  Play, and pick the options. The choice is kept in the browser.
- **On the desktop**: `./play --gl` with the options as parameters, for example
  `./play --gl --param glpoly=1 --param glscale=fit --param glfilter=1 --param gldepth=1`.

| Setting | Parameter | What it does |
|---|---|---|
| GL renderer | `glpoly=1` | GL draws the 3D polygons too. On its own, at the original's resolution, the picture is the original's but for a few edge pixels. |
| Resolution | `glscale=<N>` or `glscale=fit` | The 3D at N times 640 x 480 (up to 8), or at the window's size. Widescreen is not done: the picture stays 4:3. |
| Smooth textures | `glfilter=1` | Filtered, mip-mapped textures and smooth shading, in 24-bit colour. |
| Depth buffer | `gldepth=1` | Where the game's back-to-front sorting put a farther polygon over a nearer one, the nearer one now stays in front. The ground and the sky keep the game's own order. |
| (debugging) | `gldebug=1` | Colours the pixels where GL's is not shown: red not drawn by GL, blue not GL's, yellow changed since. |

### Pictures

Each pair is the same frame of a headless run of the first mission: the original's renderer on the left
(its 640 x 480, scaled up), the GL renderer on the right with all three options at 960 x 720. On a bigger
screen the GL renderer draws at its size.

<div class="shots pair">
  <figure><img src="/screenshots/gl/static-original.jpg" alt="The desert road in the original renderer" loading="lazy"><figcaption>The original</figcaption></figure>
  <figure><img src="/screenshots/gl/static-gl.jpg" alt="The desert road in the GL renderer" loading="lazy"><figcaption>The GL renderer</figcaption></figure>
</div>

<div class="shots wide">
  <figure><img src="/screenshots/gl/static-detail.jpg" alt="A detail: the truck and the water tower, original and GL" loading="lazy"><figcaption>Detail, twice the size: the truck and the water tower</figcaption></figure>
</div>

<div class="shots pair">
  <figure><img src="/screenshots/gl/f3000-original.jpg" alt="The 3D cockpit following a car, original renderer" loading="lazy"><figcaption>The original</figcaption></figure>
  <figure><img src="/screenshots/gl/f3000-gl.jpg" alt="The 3D cockpit following a car, GL renderer" loading="lazy"><figcaption>The GL renderer</figcaption></figure>
</div>

<div class="shots wide">
  <figure><img src="/screenshots/gl/f3000-detail.jpg" alt="A detail: the weapons panel and the damage display, original and GL" loading="lazy"><figcaption>Detail: the cockpit's panels are 3D polygons too</figcaption></figure>
</div>

<div class="shots pair">
  <figure><img src="/screenshots/gl/f5790-original.jpg" alt="An explosion ahead, original renderer" loading="lazy"><figcaption>The original</figcaption></figure>
  <figure><img src="/screenshots/gl/f5790-gl.jpg" alt="An explosion ahead, GL renderer" loading="lazy"><figcaption>The GL renderer: the fire's translucency through the same tables</figcaption></figure>
</div>

<div class="shots wide">
  <figure><img src="/screenshots/gl/f5790-detail.jpg" alt="A detail: the bonnet and the CB handset, original and GL" loading="lazy"><figcaption>Detail: the bonnet and the CB handset</figcaption></figure>
</div>

### What stays the original's

- The 2D art: menus, the dashboard's bitmaps, text, sprites and the movies, at 640 x 480.
- The shape of the terrain: the game splits the ground into finer triangles as you get near, so hills
  change shape a little as you drive. GL draws the triangles the game makes.
- The picture's 4:3 shape.

How it works, for the curious: [the port plan](https://github.com/emdzej/openi76/blob/main/docs/re/port-plan.md),
step 11.
