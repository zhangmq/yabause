# Yaba Sanshiro — libretro core for low-end ARM handhelds

**This is a fork.** It forks [`sydarn/yabause`](https://github.com/sydarn/yabause)
(a fork of [Yabause](https://github.com/Yabause/yabause) / Yaba Sanshiro by devmiyax),
pinned at [`a40dace1`](https://github.com/sydarn/yabause/commit/a40dace1)
(devmiyax `B2_1_11`, standalone 1.11.0 lineage). Branch **`h700`** is the only maintained
branch here; upstream development on this line has stopped. The **Sega Titan Video (ST-V)**
support was developed on a `stv` branch and merged into `h700` in `aa20951b` (2026-10-02); that
branch was then deleted from the remote.

It targets low-end ARM handhelds — verified on the **Anbernic RGSP**
(Allwinner H700, 4× Cortex-A53, Mali-G31 / libmali, glibc 2.35) with the NextUI/minarch-gl
frontend — and is loaded as `yabasanshiro_libretro.so`. It plays **Sega Saturn** discs and
**Sega Titan Video (ST-V)** arcade ROMs (the latter ported from the `kronos` branch of
`libretro/yabause`, see *Sega Titan Video (ST-V)* below).

## What this branch adds

**Make the abandoned port work** (`a40dace1..h700`):

* `0be6fb9e` — make the libretro port in this pin build again: libchdr wiring, `vdp1`/`vdp2`
  compiled as C++, a GLES compatibility header, `YabNanosleep`, …
* `258a192a` — **run the async VDP thread through a core-owned shared EGL context**:
  `eglCreateContext(share = frontend context)`, and the engine draws through a mirror FBO whose
  colour attachment is the frontend's texture. VDP rendering moves to a worker thread while
  `video_cb()` stays on the frontend's thread. Without it the port is synchronous and the frontend's
  thread does the rendering; with it the rendering work leaves that thread.
* `b2bb246e` — hand the frontend's GL context back after borrowing the shared one (SDL tracks
  "which context is current" per thread in TLS; bypassing it left the frontend without a context).
* **Renderer mode is a runtime setting** (see below): both renderers are compiled in and the core
  option *Async Renderer* picks one per boot, instead of a `YAB_SYNC=1` rebuild. The async path is
  the default; the inline path keeps the emulated machine off the host scheduler entirely, which is
  occasionally useful when bisecting.
* `9cceb4e3` — **VDP1 framebuffer readback is on demand** (core option *VDP1 Framebuffer
  Readback*, default `disabled`): the async renderer records the completed drawframe and downloads
  it only when the SH-2 actually reads; a short read-triggered prefetch avoids blocking per frame.
  Titles that never read are unaffected; `batmanfr` keeps it off.

**GL / correctness on this driver** (textures are shared between the two contexts, FBOs are not):

* `074c4034` — give the core-owned mirror FBO the **depth and stencil** the engine's VDP2
  compositor needs: it clears depth to 0 and draws every layer with `GL_GEQUAL` + per-layer z,
  and implements VDP2 windows through the stencil buffer. Without them a windowed background layer
  covers the whole screen (Demon Castle Dracula X: walls and stairs vanish).
* `9e9593f6` — re-assert the mirror's attachments on every hand-back, and re-read the frontend's
  colour attachment every frame (a frontend may recreate its FBO under the same name).
* `caf6c0f2` — size that attachment to the **render area**, not to the frontend's power-of-two
  texture. A framebuffer is as large as the intersection of its attachments and the engine's
  per-frame clear is not scissored, so sizing it to the render area avoids a full-size
  depth+stencil clear every frame.
* `26364f71` — resolve the ES 3.1 entry points (`glBindImageTexture`/`glDispatchCompute`/
  `glMemoryBarrier`) that the RBG compute path calls; without them Virtua Fighter 2's hi-res title
  screen crashed the core about 9 s in (`PC=0`).

**libretro feature parity with the reference core** (the `yabasanshiro` branch of
[`libretro/yabause`](https://github.com/libretro/yabause/tree/yabasanshiro), tip `09ed8e5b`):

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
* `cc7ee800` — **keep the VDP1 shadow-discard test as upstream writes it** (`fbColor.b != 0.0`).
  An alignment with the reference core's `>= 0.45` (`6cf146a3`, briefly restored by `843509a2`) was
  reverted for good: on-screen inspection showed Castlevania SotN losing its walls, and although the
  missing depth/stencil on the mirror FBO (`074c4034`) turned out to be the real cause of that
  regression, a three-colour shader probe plus save-state-pinned comparisons then showed the two
  spellings cannot differ on anything reachable here (the disputed range `b ∈ (0, 0.45)` never
  occurs), so the upstream form stays. Do not "align" it again without a scene that disagrees.

**SCSP threading** (`68821ac4`, `8fd15b47`, `b000abc0`) — the async sound CPU thread used
`ctime()` as a `struct timespec *`, so its timed wait returned `EINVAL` immediately and the thread
spun; fixing the clock source restored the on-demand synchronisation path. The pending main-CPU
interrupt word is atomic now. An optional realtime priority switch exists
(`YAB_SCSP_RT_PRIO`/`YAB_SCSP_RT_NICE`, off by default).

### Renderer mode is a runtime setting

Both renderers are compiled into one core and selected by the core option **Async Renderer**
(`yabasanshiro_async_rendering`, category *Video*, default `enabled`). It is latched when a game is
loaded -- the GL context and the render worker are created once per machine boot -- so a change
takes effect on the **next launch** of a game ("Core restart required").

* `enabled` (default) -- VDP1/VDP2 rendering runs on its own thread through the core-owned shared
  context described above. The default configuration, and what this fork is about.
* `disabled` -- the frame loop draws inline with the frontend's context, exactly like upstream
  Yabause. Synchronous: the emulated machine no longer depends on host scheduling.

**A black screen while the emulator still presents frames was an ST-V bus race, not a
renderer problem.** Some Titan titles booted black with the async renderer and never with the inline
one, which made the renderer look guilty; the actual cause was the SCU DMA writing SoundRam while
the SH-2 still owned the bus, so the CPU read back half-uploaded data (`7d8552b5`, ported from
MAME's bus arbitration). With that fixed the async renderer boots those titles like the inline one,
and switching renderers is no longer a workaround for anything.

There is no build-time switch left: `make platform=arm64 YAB_SYNC=1` no longer changes anything
(the defines it removes no longer gate code). For A/B runs the environment variable
`YAB_VDP_SYNC=1` forces the inline path.

## Sega Titan Video (ST-V)

Ported from the **`kronos` branch of [`libretro/yabause`](https://github.com/libretro/yabause/tree/kronos)**
(tip `3791ffb`, "Merge pull request #328 from WizzardSK/aarch64-kronos"; that branch is in turn the
libretro port of [`FCare/Kronos`](https://github.com/FCare/Kronos)). Both are GPL-2.0, like this
tree. What came over:

* `stv.c`/`stv.h` — the ST-V BIOS table (`BiosList`: file name + CRC32 + region) and the game table
  (`GameList`, 103 titles), zip ROM/BIOS loading, the 48 MiB ROM-board image, the NV-RAM template.
* `eeprom.c`/`eeprom.h` and the SMPC bit-bang that drives it (PDR1 `0x3f` write, DO bit read back
  through PDR2) — without it the ST-V BIOS spins forever waiting for the EEPROM.
* `decrypt.c`/`decrypt.h`, the cs1 decryption channel in `cs0.c`, and `junzip.c`/`junzip.h` for
  reading game ROMs and `stvbios.zip` straight out of the zip.
* `cs0.c`'s `CART_ROMSTV` cartridge type (48 MiB) and the `memory.c` DMA read path the decryption
  needs.
* The **IOGA / JAMMA cabinet input**: `peripheral.c` keeps `IOPORT`/`PerCabAdd` and the games read
  it at `0x04000000` (`0x01` P1, `0x03` P2, `0x05` system = coin/test/service/START); `libretro.c`
  binds the frontend's joypad to the `PERJAMMA_*` keys.
* The `memory.c`/`smpc.c`/`scu.c`/`yabause.c` alignments the port needs (`yabsys.isSTV` gating of
  the Saturn-only init paths, the OREG[31] handshake, the PDR2 sound-CPU wire) and the libretro
  hooks: `stv_mode` detected from the zip entry names + CRC32, `stv_favorite_region`, the ST-V key
  descriptors.

**ST-V fixes that came after the initial port**: the SCU-DMA/SH-2 bus race above (`7d8552b5`);
the CS1 protection port that the Saturn cart-ID special case used to short-circuit, which left
three encrypted titles looping on a garbage read and is now gated by cartridge type (issue #9);
unimplemented IOGA offsets reading `0xFF` like MAME instead of `0x00`; 32-bit reads of the VDP2
register file; and romset matching that prefers the exact zip name over the last matching entry
(issue #13).

**Two ST-V options exist because a few carts are region- or timing-sensitive**:

* `yabasanshiro_stv_region` = `EU` (default) / `US` / `JP` / `TW` — picks a BIOS from `stvbios.zip`
  among the regions the cart header allows, exactly like Kronos' `kronos_stv_favorite_region`
  (there is deliberately no "Auto": the header only lists what is *allowed*, and the region changes
  gameplay, not just language). `Radiant Silvergun` is the case in point: on a JP BIOS it runs its
  three-button, seven-weapon set; on any western BIOS it enters the two-button "overseas" mode with
  no Weapon C. Set `JP` for that one.
* `yabasanshiro_stv_sound_cpu_lead` = `off` (default) / `half_frame` — **experimental, temporary**.
  Holds the 68k sound CPU half a frame ahead of the SH-2 frame. `Sea Bass Fishing V0.001` only
  cold-boots with it. It is an empirical workaround, not a timing model, and it will be
  **removed once the boot is fixed properly** — the intended use meanwhile is: enable it for the
  boot, save a state, turn it off (or load that state with it off). Everything else about the boot
  stays a known limitation of the emulator, not a feature.

Two deliberate differences from Kronos:

* **The backup-RAM card layout stays the classic yabause one** (`header[32]` = `FF 'B' FF 'a' …`,
  card byte N at offset `2N+1`). Kronos' dense layout made every `<rom>.sav` written by an earlier
  build unreadable, and a real BIOS then answers *"The System Memory is not ready for use. Please
  clear all files using Sega Saturn's Memory Manager."* (Guardian Heroes, Castlevania SotN).
  `6407c37d` reverts just that group; the `extend_backup` value and the real word/long handlers
  from the same commit are kept.
* **`SmpcSetTiming()` gives the INTBACK case a timing for every `IREG[0]`** (`56753e92`). The old
  code handled only `IREG[0] == 0x01`/`0x00`; cotton2 asks with `0xFF`/`0x80`, so `SmpcExec()`
  never ran the command, `SR` stayed 0 and the game never read the pad. That is why the
  picture-good line never saw coin or START (the only build that did was the full Kronos SMPC port,
  which came with a black picture and a deadlock at the resolution switch). Coin, START and the
  in-game 1P start were verified on this tree afterwards.

To run an ST-V game, put `stvbios.zip` in the frontend's **system** directory (next to
`saturn_bios.bin`) and the game zips in the ROM directory. The board is recognised from the zip
entry names + CRC32, so a re-zipped or renamed `stvbios.zip` silently falls back to Saturn mode.
`yabasanshiro_libretro.info` declares that file the way Kronos' `.info` does (`firmware1`, optional,
because Saturn titles do not need it), so RetroArch's *Information → Firmware* list names it too.

The core advertises `block_extract = true`, so the frontend has to hand the romset **zip over
unextracted** — the core opens it itself (RetroArch would otherwise extract a member to
`/tmp/cache/…` and the board detection would never run). A side effect is that Saturn CD images
must be uncompressed (`.cue`/`.bin`/`.iso`/`.chd`); a zipped disc is not something this core can
mount.

The ST-V **SRAM is per game**: `<save>/stv/<game>.ram`, next to the `<game>.nv` EEPROM, matching
Kronos' naming (the layout is still our classic 64 KB card image, not Kronos' dense 32 KB one). A
Saturn keeps one shared internal memory card, as on hardware. Making that file actually appear
re-exposed a latent bug: `bupfilename` was assigned only on the `extend_backup` path, so with the
`extend_backup = 0` the ST-V credit fix needs, `YabauseDeInit()` called `T123Save(..., NULL)` and
the core never persisted its card at all — only the frontend's per-ROM `.sav` carried it.

The old probe/recorder infrastructure from the porting effort (SMPC/IOGA loggers, black-box
recorders, per-frame stat dumps, the `autopress.txt` hook) was removed. The diagnostics that remain
are environment-gated and off by default.

## Building

```sh
cd yabause/src/libretro

# one build, both renderers inside; the mode is a core option at runtime
make platform=arm64
```

(Before this branch made them the default you had to pass them yourself, and only in the environment
-- a `CFLAGS=` on the command line overrides the makefile's own `CFLAGS += $(FLAGS)` and would drop
the platform flags (`-DAARCH64`, `-D_OGLES3_`, `-I…`, `-D__LIBRETRO__`, `-std=gnu99`, …).)

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

* With the async renderer the core renders into a **core-owned mirror FBO** over the colour texture
  the frontend hands it (FBOs are not shared between the two EGL contexts on this driver, textures
  are). It attaches its own depth+stencil, sized to the render area. With the inline renderer
  (`Async Renderer = disabled`) the engine draws straight into the frontend's FBO and none of this
  applies: `get_current_framebuffer()` is used as-is.
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

## Performance

Performance numbers are intentionally not published while the async renderer and the VDP1
readback path are still being changed.  Final figures will only be documented once the
implementation is frozen.


## Upstream, credits and license

* Upstream: [sydarn/yabause](https://github.com/sydarn/yabause) → devmiyax's Yaba Sanshiro →
  [Yabause](https://github.com/Yabause/yabause). The upstream website (uoyabause.org) is
  offline.
* **ST-V support is ported from the `kronos` branch of
  [`libretro/yabause`](https://github.com/libretro/yabause/tree/kronos)** (tip `3791ffb`), which is
  the libretro port of [FCare/Kronos](https://github.com/FCare/Kronos). The `stv.c`/`eeprom.c`/
  `decrypt.c`/`junzip.c` files, the cs0/cs1 cartridge and decryption channels, the IOGA/JAMMA
  input model and the SMPC/peripheral alignments come from there (GPL-2.0, same as this tree).
* The upstream tree's own documentation is kept as-is: `yabause/README`, `README.LIN`,
  `README.QT`, `README.WIN`, `README.DC`, `README.MAC` and `yabause/doc/`.
* Upstream community, not maintained by this fork:
  [Discord](https://discord.gg/aRJhTBH) · [Liberapay](https://liberapay.com/devmiyax).
  The badges that used to be at the top of this file were removed: they reported the upstream
  author's Travis / AppVeyor / Snapcraft accounts, and none of those report anything for this
  branch.
* License: **GPL-2.0** ([`LICENSE`](LICENSE), unchanged). Everything added on this branch is
  GPL-2.0 as well.
