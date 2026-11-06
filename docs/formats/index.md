# File formats

One page per format, with exact layouts, marked confirmed (checked against the data, and the exe where it
matters) or inferred. The census of what exists is in [../re/inventory.md](../re/inventory.md).

| Format | Where | Page | State |
|---|---|---|---|
| ZFS archive | `I76.ZFS` | [zfs.md](zfs.md) | confirmed; reader in `src/vfs_zfs.c` |
| ZIX file index | `I76MIN.ZIX`, `I76MAX.ZIX` (installed as `i76.zix`) | [zix.md](zix.md) | confirmed layout; how the game finds every file |
| PCX | `LOADSCR.PCX`, `LOADGAME.PCX` | (`src/pcx.h`) | standard 8-bit RLE; matches ffmpeg |
| BWD2 container | `.msn .sdf .vdf .vcf .wdf .gdf .xdf .vtf .cdf` | [bwd2.md](bwd2.md) | container confirmed; payloads to do |
| Palettes and images | `.act`, `.map`, `.cbk` + `.vqm` | [textures.md](textures.md) | confirmed; decoders in `src/fmt/image.c` |
| Bundles | `.pak` + `.pix` (meshes, textures, tables) | [textures.md](textures.md) | confirmed: 884 bundles, 13,585 members |
| Meshes | `.geo` | [geo.md](geo.md) | confirmed; decoder in `src/fmt/geo.c` |
| Vehicles | `.vdf` (VGEO), `.vtf`, `.tmt` | [vehicles.md](vehicles.md) | VGEO, paint slots and tables confirmed; other chunks to do |
| Tables | `.lum`, `.tbl`, `.msk` | [textures.md](textures.md) | to do |
| Terrain | `.ter` | | to do |
| Sound | `.wav` (RIFF PCM), `.gpw` (`GAS0`) | | to do |
| Text | `.npt`, `.rtm`, `.fsi`, `*.map`/`*.def` key bindings | | to do |
| Smacker 2 movies | `CUTSCENE/*.SMK` | | to do (step 10) |
| Unknown | `DATABASE.MW2`, `CDROM.SPD` | | to identify |
