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

* `0be6fb9e` — make the abandoned libretro port in this pin build again (libchdr wiring,
  `vdp1`/`vdp2` compiled as C++, GLES compatibility header, `Resize(..., FULL)`,
  `YabNanosleep`, …).
* `258a192a` — **render the async VDP thread through a core-owned shared EGL context**: the
  core creates `eglCreateContext(share = frontend context)` and draws through a mirror FBO
  attached to the frontend's colour texture, so VDP rendering runs on a worker thread while
  `video_cb()` is still issued from the frontend thread. This is what lets the emulator use
  more than one CPU core; without it the port is synchronous and single-core bound.

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

# asynchronous build (what we ship)
make platform=arm64 \
     CFLAGS="-DYAB_ASYNC_RENDERING -DYAB_CORE_SHARED_CONTEXT" \
     CXXFLAGS="-DYAB_ASYNC_RENDERING -DYAB_CORE_SHARED_CONTEXT"
```

`platform=arm64` selects `-DAARCH64 -march=armv8-a+crc+fp+simd -mcpu=cortex-a53`,
`FORCE_GLES=1`, `-D_OGLES3_ -DHAVE_OPENGLES -DHAVE_OPENGLES3` and `HAVE_GLSYM_PRIVATE`.
Output: `yabause/src/libretro/yabasanshiro_libretro.so`.

Two non-obvious requirements on these devices:

* **Link `libmali` explicitly** (`-l:libmali.so.0`): `libGLESv2.so.2` on the device is a stub
  that exports no GL entry points; the real symbols live in `libmali.so.0`.
* **Add `-ldl` when the sysroot is glibc < 2.34** (for example the gcc 8.3 / glibc 2.28
  toolchain used to build the NextUI cores): the shared-context path uses `dlopen`/`dlsym`,
  and `libdl` is still a separate library there. Newer sysroots merge it into libc, so the
  same source can build locally without it.

The built file must keep an `_` in its name — frontends derive the per-core configuration
directory from the part before the first `_`.

## Measured

RGSP, CPU governor pinned to `performance`, 120 s windows, offscreen harness, median fps:

| Build | Golden Axe | Daytona USA |
|---|---|---|
| synchronous | 45.7 | 33.7 |
| async + shared context | **58.6** (+28 %) | **37.8** (+12 %) |

Known cost: the worker performs one `glFinish()` per frame (cross-context synchronisation).

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
