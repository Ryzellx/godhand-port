# Kontrak Argumen — Engine ↔ Launcher

Launcher memanggil binary dengan urutan flag tetap. Engine wajib mem-parse semuanya
(flag tak dikenal = warning, jangan crash).

```
<binary> \
  --renderer   vulkan|d3d12|opengl|software \
  --resolution 3840x2160 \
  --mode       windowed|borderless|fullscreen \
  --scale      1.0 … 4.0 \
  --vsync      0|1 \
  --fps        0|60|120|144 \
  --msaa       1|2|4|8 \
  --aniso      1|2|4|8|16 \
  --texfilter  nearest|linear \
  --widescreen 0|1 \
  --iso        /path/to/God_Hand.iso
```

## Env yang disarankan

| Env | Isi |
|---|---|
| `GH_ISO` | path ISO (redundan dengan `--iso`, buat fallback) |
| `GH_LOG` | path file log engine (launcher set ke `~/.config/godhand-launcher/launch.log`) |

## Exit code

`0` = normal keluar · non-zero = launcher tampilkan di console + tulis ke `launch.log`.

## Status wiring

| Bagian | Status |
|---|---|
| ISO picker + validasi disc | ✅ jalan (teruji di ISO asli) |
| Settings persist | ✅ jalan |
| Keybind capture | ✅ jalan |
| Gamepad detect + remap | ✅ jalan (butuh controller fisik untuk uji) |
| Auto-detect binary | ✅ jalan (scan `build/`, root, `release/`) |
| PLAY → spawn engine | ✅ kode siap, **menunggu binary port ada** |
