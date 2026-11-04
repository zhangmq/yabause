# Yaba Sanshiro — libretro core (archived)

> ## ⚠ Archived — no longer maintained
>
> This repository is kept **for reference only**. There are no updates, no issue
> triage and pull requests are **not merged**.

## Where the work continues

The libretro core is being implemented on top of the upstream
[pirrypirrypirry/yabasanshiro-pirry](https://github.com/pirrypirrypirry/yabasanshiro-pirry)
tree, and the **ST-V (Sega Titan Video) support from this repository will be
migrated there** as well.

👉 **Current work: https://github.com/zhangmq/yabasanshiro-pirry/tree/h700-libretro**

The standalone (retro_arena) emulator sources and standalone builds are no longer
kept or maintained here.

## What this repository was

A fork of [`sydarn/yabause`](https://github.com/sydarn/yabause) — itself a fork of
[Yabause](https://github.com/Yabause/yabause) / Yaba Sanshiro by devmiyax — pinned
at [`a40dace1`](https://github.com/sydarn/yabause/commit/a40dace1) (devmiyax
`B2_1_11`, standalone 1.11.0 lineage), turned into a **libretro core for low-end
ARM handhelds** and verified on Allwinner H700 devices (4× Cortex-A53, Mali-G31 /
libmali, glibc 2.35) with the NextUI/minarch-gl frontend.

It plays **Sega Saturn** discs and **Sega Titan Video (ST-V)** arcade ROMs, loaded
as `yabasanshiro_libretro.so`.

Work that was developed here and is the reason the tree is kept for reference:

- **The abandoned port made buildable again** — libchdr wiring, `vdp1`/`vdp2`
  compiled as C++, a GLES compatibility header, `YabNanosleep`, …
- **The async VDP thread run through a core-owned EGL context** created with
  `eglCreateContext(share = frontend context)`, drawing into a mirror FBO whose
  colour attachment is the frontend's texture — textures are shared across
  contexts, FBOs are not, so the mirror FBO carries its own depth/stencil.
- **CHD loading fix** — the 2019 libchdr in libretro-common cannot decode the
  codec used by these images; the standalone's libchdr can.
- **ST-V support** — romset detection by zip entry name + CRC32, cart mapping,
  decrypt, JAMMA key mapping, region option.
- Renderer/audio/timing work: VDP1 readback, SCSP sample path, frame pacing,
  save states, and a long alignment pass against pirry 1.6.2.

## Branch notes

- `h700` — the maintained branch (default). Last merge: PR #29 (`65f2a612`).
- `pirry-align` — the later alignment work against pirry 1.6.2 (`55ec229b`).
- `stv`, `perf-absorb-pirry`, `batmanfr-rax`, `probe-*` — historical feature and
  investigation branches, kept as-is.

## Licence / lineage

Upstream lineage is preserved. See `LICENSE` and `THIRD-PARTY-NOTICES.md`.
