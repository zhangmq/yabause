# Yaba Sanshiro — libretro core (archived)

> ## ⚠ Archived — no longer maintained
>
> This repository is kept **for reference only**. There are no updates, no issue
> triage and pull requests are **not merged**.

## What this repository is

A fork of [`sydarn/yabause`](https://github.com/sydarn/yabause) — itself a fork of
[Yabause](https://github.com/Yabause/yabause) / Yaba Sanshiro by devmiyax — pinned
at [`a40dace1`](https://github.com/sydarn/yabause/commit/a40dace1) (devmiyax
`B2_1_11`, standalone 1.11.0 lineage).

It was forked **to implement a libretro core** for low-end ARM handhelds, verified
on Allwinner H700 devices (4× Cortex-A53, Mali-G31 / libmali, glibc 2.35) with the
NextUI/minarch-gl frontend, loading as `yabasanshiro_libretro.so`.

Only the **libretro path** was developed here — `yabause/src/libretro/` plus the
engine-side hooks it needs. The standalone (`retro_arena`) frontend was **not**
touched: it is part of the upstream tree and simply stays as it is.

## Why development stopped here

The libretro core will not be developed on this base any more. The
[pirrypirrypirry/yabasanshiro-pirry](https://github.com/pirrypirrypirry/yabasanshiro-pirry)
tree (Yaba Sanshiro 1.6.2) is better in every respect, so our work is being ported
there instead of continuing here.

We first tried to bring pirry's optimizations into this tree, but the two code
bases had diverged too far — the port was too invasive to be worth it — so the
decision was to start from the pirry tree and move our changes onto it.

## Where the work continues

👉 **https://github.com/zhangmq/yabasanshiro-pirry/tree/h700-libretro**

The **ST-V (Sega Titan Video)** support developed here will be migrated to that
repository as well.

## What was done here (kept for reference)

- **The abandoned libretro port made buildable again** — libchdr wiring,
  `vdp1`/`vdp2` compiled as C++, a GLES compatibility header, `YabNanosleep`, …
- **The async VDP thread run through a core-owned EGL context** created with
  `eglCreateContext(share = frontend context)`, drawing into a mirror FBO whose
  colour attachment is the frontend's texture — textures are shared across
  contexts, FBOs are not, so the mirror FBO carries its own depth/stencil.
- **CHD loading fix** — the 2019 libchdr in libretro-common cannot decode the codec
  used by these images; the standalone's libchdr can.
- **ST-V support** — romset detection by zip entry name + CRC32, cart mapping,
  decrypt, JAMMA key mapping, region option.
- Renderer/audio/timing work: VDP1 readback, SCSP sample path, frame pacing, save
  states.

## Branch notes

- `h700` — the maintained branch (default). Last merge: PR #29 (`65f2a612`).
- `pirry-align` — the later alignment pass against pirry 1.6.2 (`55ec229b`).
- `stv`, `perf-absorb-pirry`, `batmanfr-rax`, `probe-*` — historical feature and
  investigation branches, kept as-is.

## Licence / lineage

Upstream lineage is preserved. See `LICENSE` and `THIRD-PARTY-NOTICES.md`.
