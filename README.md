# Yaba Sanshiro — libretro core for low-end ARM handhelds

**This is a fork.** It forks [`sydarn/yabause`](https://github.com/sydarn/yabause)
(a fork of [Yabause](https://github.com/Yabause/yabause) / Yaba Sanshiro by devmiyax),
pinned at [`a40dace1`](https://github.com/sydarn/yabause/commit/a40dace1)
(devmiyax `B2_1_11`, standalone 1.11.0 lineage). Branch **`h700`** is the only maintained
branch here; upstream development on this line has stopped.

It targets low-end ARM handhelds — verified on the **Anbernic RGSP**
(Allwinner H700, 4× Cortex-A53, Mali-G31 / libmali, glibc 2.35) with the NextUI/minarch-gl
frontend — and is loaded as `yabasanshiro_libretro.so`.

## What this branch adds

**Make the abandoned port work, and make it fast** (`a40dace1..h700`):

* `0be6fb9e` — make the libretro port in this pin build again: libchdr wiring, `vdp1`/`vdp2`
  compiled as C++, a GLES compatibility header, `YabNanosleep`, …
* `258a192a` — **run the async VDP thread through a core-owned shared EGL context**:
  `eglCreateContext(share = frontend context)`, and the engine draws through a mirror FBO whose
  colour attachment is the frontend's texture. VDP rendering moves to a worker thread while
  `video_cb()` stays on the frontend's thread. Without it the port is synchronous and pinned to
  one CPU core; with it the emulation thread drops from ~1.0 to ~0.7 cores and the frame rate
  gains 9–28 % depending on the game (see *Measured*).
* `b2bb246e` — hand the frontend's GL context back after borrowing the shared one (SDL tracks
  "which context is current" per thread in TLS; bypassing it left the frontend without a context).

**GL / correctness on this driver** (textures are shared between the two contexts, FBOs are not):

* `074c4034` — give the core-owned mirror FBO the **depth and stencil** the engine's VDP2
  compositor needs: it clears depth to 0 and draws every layer with `GL_GEQUAL` + per-layer z,
  and implements VDP2 windows through the stencil buffer. Without them a windowed background layer
  covers the whole screen (Demon Castle Dracula X: walls and stairs vanish).
* `9e9593f6` — re-assert the mirror's attachments on every hand-back, and re-read the frontend's
  colour attachment every frame (a frontend may recreate its FBO under the same name).
* `caf6c0f2` — size that attachment to the **render area**, not to the frontend's power-of-two
  texture. A framebuffer is as large as the intersection of its attachments and the engine's
  per-frame clear is not scissored, so this removes a 4 MB depth+stencil clear per frame: the
  depth/stencil work is now free (back to the pre-fix frame rate).
* `26364f71` — resolve the ES 3.1 entry points (`glBindImageTexture`/`glDispatchCompute`/
  `glMemoryBarrier`) that the RBG compute path calls; without them Virtua Fighter 2's hi-res title
  screen crashed the core about 9 s in (`PC=0`).

**libretro feature parity with the reference core** ([`lr-yabasanshiro`](https://github.com/libretro/yabasanshiro)):

* `8d8cab15`, `247ee0f4`, `ea028ee4` — savestates, plus `SAVE_RAM`/`SYSTEM_RAM` and the Saturn
  memory map. `ea028ee4` also fixes *silent audio when loading a state on a cold start*: the SCSP
  chunk never carried the sound CPU's registers (the Musashi hooks were empty) and restoring SP
  before SR left the CPU in the wrong mode. Savestate SCSP chunk version 4 → 5; **a core older
  than this cannot read new states** (re-save if you switch back).
* `5e408a8b` — `resolution_mode = original` maps to `RES_NATIVE` (draw straight into the
  framebuffer) and `Resize(..., ORIGINAL)`, like the reference core, whose comment notes the
  upscale path is broken under a libretro HW context.
* `c42b9781` — core options v2 (categories, submenus, visibility, translations), `libretro.h`
  up to the v2 option commands, `yabasanshiro_libretro.info`.
* `50e91159` — the disk control interface (v1 + extended) and `.m3u` playlists, so multi-disc
  games swap discs from the frontend menu.
* `610bb618` — the reference core's low-risk robustness fixes (`error.c` allocation, the thread
  CPU-index mutex, `BackupManager` flush, HLE BIOS default Slave-CPU proc, `debug.c` log mutex).
* `6cf146a3` (restored by `843509a2`) — align the two rendering differences with the reference
  core: the VDP1 shadow-discard threshold and the RGB shader bit ops. An earlier revert
  (`cfcba53f`) had blamed the threshold for SotN's missing walls; that premise was disproven —
  the walls were lost to the missing depth/stencil on the async mirror FBO (`074c4034`).

**SCSP threading** (`68821ac4`, `8fd15b47`, `b000abc0`) — the async sound CPU thread used
`ctime()` as a `struct timespec *`, so its timed wait returned `EINVAL` immediately and the thread
span at ~1.09 cores; fixing the clock source restored the on-demand synchronisation path
(Golden Axe: 2.57 → 1.87 cores, +2 % frames). The pending main-CPU interrupt word is atomic now.
An optional realtime priority switch exists (`YAB_SCSP_RT_PRIO`/`YAB_SCSP_RT_NICE`, off by default:
it measured neutral on this device).

Two build switches, both off by default, both required for the async build:

| Define | Meaning |
|---|---|
| `YAB_ASYNC_RENDERING` | upstream's VDP render thread (its `#define` in `vdp1.h` is commented out) |
| `YAB_CORE_SHARED_CONTEXT` | the core-owned shared context added here (no-op when undefined) |

## Building

```sh
cd yabause/src/libretro

# synchronous build
make platform=arm64

# asynchronous build (what this branch ships)
CFLAGS="-DYAB_ASYNC_RENDERING -DYAB_CORE_SHARED_CONTEXT" \
CXXFLAGS="-DYAB_ASYNC_RENDERING -DYAB_CORE_SHARED_CONTEXT" \
make platform=arm64
```

Pass the defines **in the environment**, not as `make CFLAGS=…`: a `CFLAGS` on the command line
overrides the makefile's own `CFLAGS += $(FLAGS)`, which would drop the platform flags
(`-DAARCH64`, `-D_OGLES3_`, `-I…`, `-D__LIBRETRO__`, `-std=gnu99`, …).

`platform=arm64` selects `-DAARCH64 -march=armv8-a+crc+fp+simd -mcpu=cortex-a53`, `FORCE_GLES=1`,
`-D_OGLES3_ -DHAVE_OPENGLES -DHAVE_OPENGLES3` and `HAVE_GLSYM_PRIVATE`.
Output: `yabause/src/libretro/yabasanshiro_libretro.so`.

Cross-compiling for the device, two requirements are easy to miss:

* **Link `libmali` explicitly** (`-l:libmali.so.0` in `LDFLAGS`): `libGLESv2.so.2` on the device
  is a stub that exports no GL entry points; the real symbols live in `libmali.so.0`.
* **Add `-ldl` when the sysroot is glibc < 2.34** (for example the gcc 8.3 / glibc 2.28 toolchain
  used to build the NextUI cores): the shared-context path uses `dlopen`/`dlsym` and `libdl` is
  still a separate library there. Newer sysroots merge it into libc, so the same source can build
  locally without it.

The built file must keep an `_` in its name when you drop it into a frontend: minarch derives the
per-core configuration directory from the part before the first `_` (and segfaults without one).

## Notes for frontends

* The core renders into a **core-owned mirror FBO** over the colour texture the frontend hands it
  (FBOs are not shared between the two EGL contexts on this driver, textures are). It attaches its
  own depth+stencil, sized to the render area.
* It asks for `depth` **and** `stencil` in `retro_hw_render_callback`; a frontend that builds the
  per-slot FBOs itself should attach `GL_DEPTH24_STENCIL8` (RetroArch's `gl3` and NextUI's
  minarch-gl do).
* If the frontend rotates hw-render FBOs between slots (RetroArch's `gfx/video_thread_hw.c` ring,
  minarch-gl's three-slot ring), the core follows it: it re-reads
  `hw_render.get_current_framebuffer()` on every `retro_run` and re-points its mirror at the new
  texture. A frontend must **not** rotate slots for a core that caches the framebuffer once at
  `GLSM_CTL_STATE_SETUP` — glsm-based cores (flycast, mupen64plus_next) do exactly that and would
  render into a slot nobody presents.
* `video_cb(RETRO_HW_FRAME_BUFFER_VALID)` is issued on the frontend's thread; the render worker
  only `glFinish()`es and marks the frame, so the frontend keeps its own context for its shader
  chain and swap.

## Measured

Anbernic RGSP, CPU governor pinned to `performance` (1512 MHz on all four cores), 120 s windows.

Offscreen harness (`noreadback`, no presentation cost), median fps:

| Build | Golden Axe | Daytona USA |
|---|---|---|
| synchronous | 45.7 | 33.7 |
| async + shared context | **58.6** (+28 %) | **37.8** (+12 %) |

On screen (minarch-gl, hardware render, frameskip disabled, debug HUD off), fps at frame 3600:

| Build | Golden Axe | Daytona USA |
|---|---|---|
| synchronous | 52.2 | 39.0 |
| async + shared context | **58.4** (+8.8 %) | **40.3** (+3.3 %) |

The emulation thread runs at ~0.70–0.84 cores instead of the synchronous build's ~1.0 (capped at
one core), which is where the gain comes from; the render worker holds its own core.

Known costs: the worker performs one `glFinish()` per frame (cross-context synchronisation), and
the presentation path adds a normalise pass in the frontend.

## Upstream, credits and license

* Upstream: [sydarn/yabause](https://github.com/sydarn/yabause) → devmiyax's Yaba Sanshiro →
  [Yabause](https://github.com/Yabause/yabause). The upstream website (uoyabause.org) is
  offline.
* The upstream tree's own documentation is kept as-is: `yabause/README`, `README.LIN`,
  `README.QT`, `README.WIN`, `README.DC`, `README.MAC` and `yabause/doc/`.
* Upstream community, not maintained by this fork:
  [Discord](https://discord.gg/aRJhTBH) · [Liberapay](https://liberapay.com/devmiyax).
  The badges that used to be at the top of this file were removed: they reported the upstream
  author's Travis / AppVeyor / Snapcraft accounts, and none of those report anything for this
  branch.
* License: **GPL-2.0** ([`LICENSE`](LICENSE), unchanged). Everything added on this branch is
  GPL-2.0 as well.
