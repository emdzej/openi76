# File formats

One page per format, with exact layouts, marked confirmed (checked against the data, and the exe where it
matters) or inferred. The census of what exists is in [../re/inventory.md](../re/inventory.md).

| Format | Where | Page | State |
|---|---|---|---|
| ZFS archive | `I76.ZFS` | [zfs.md](zfs.md) | confirmed; reader in `src/vfs_zfs.c` |
| ZIX file index | `I76MIN.ZIX`, `I76MAX.ZIX` (installed as `i76.zix`) | [zix.md](zix.md) | confirmed layout; how the game finds every file |
| PCX | `LOADSCR.PCX`, `LOADGAME.PCX` | (`src/pcx.h`) | standard 8-bit RLE; matches ffmpeg |
| BWD2 container | `.msn .sdf .vdf .vcf .wdf .gdf .xdf .vtf .cdf` | | to do (step 2) |
| Palettes and tables | `.act`, `.lum`, `.tbl` | | to do |
| Textures | `.map`, `.vqm` + `.cbk`, `.pak` + `.pix`, `.msk` | | to do |
| Meshes | `.geo` (and inside `.sdf`/`.vdf`) | | to do |
| Terrain | `.ter` | | to do |
| Sound | `.wav` (RIFF PCM), `.gpw` (`GAS0`) | | to do |
| Text | `.npt`, `.rtm`, `.fsi`, `*.map`/`*.def` key bindings | | to do |
| Smacker 2 movies | `CUTSCENE/*.SMK` | | to do (step 10) |
| Unknown | `DATABASE.MW2`, `CDROM.SPD` | | to identify |
