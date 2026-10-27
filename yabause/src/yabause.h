/*  Copyright 2003-2005 Guillaume Duhamel
    Copyright 2004-2006 Theo Berkau
    Copyright 2006      Anders Montonen

    This file is part of Yabause.

    Yabause is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    Yabause is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Yabause; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
*/
/*
        Copyright 2019 devMiyax(smiyaxdev@gmail.com)

This file is part of YabaSanshiro.

        YabaSanshiro is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

YabaSanshiro is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

        You should have received a copy of the GNU General Public License
along with YabaSanshiro; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
*/

#ifndef YABAUSE_H
#define YABAUSE_H

#include "core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
   int percoretype;
   int sh2coretype;
   int vidcoretype;
   int sndcoretype;
   int m68kcoretype;
   int cdcoretype;
   int carttype;
   u8 regionid;
   const char *biospath;
   const char *cdpath;
   const char *ssfpath;
   const char *buppath;
   const char *mpegpath;
   const char *cartpath;
   const char *modemip;
	const char *modemport;
   int videoformattype;
   int frameskip;
   int framelimit; // 0 .. 60Hz, 1 .. no limit, 2 .. 2x(120Hz)
   int clocksync;  // 1 = sync internal clock to emulation, 0 = realtime clock
   u32 basetime;   // Initial time in clocksync mode (0 = start w/ system time)
   int usethreads;
   int numthreads;
   int osdcoretype;
   int skip_load;//skip loading in YabauseInit so tests can be run without a bios
   int video_filter_type;
   int polygon_generation_mode;
   int play_ssf;
   int use_new_scsp;
   int resolution_mode;
   int rbg_resolution_mode;
   int rbg_use_compute_shader;
   int extend_backup;
   int rotate_screen;
   int scsp_sync_count_per_frame;
   int scsp_main_mode;
   u32 sync_shift;
   const char *playRecordPath;
   int use_cpu_affinity;
   int use_sh2_cache;
   /* ST-V (Sega Titan Video) -- ported from libretro/yabause@kronos */
   const char *stvgamepath;      /* the .zip romset, for STVSingleInit() */
   const char *stvgame;          /* romset basename, for STVInit() */
   const char *stvbiospath;      /* stvbios.zip */
   const char *eepromdir;        /* where the per-game .nv lives */
   int stv_favorite_region;
} yabauseinit_struct;

/* Renderer mode: 1 = async VDP worker (default), 0 = draw inline (sync).
 * Latched at load time; see the comment on the definition in yabause.c. */
extern int g_vdp_async;

/* VDP1 framebuffer readback (core option "yabasanshiro_vdp1_fb_readback", off
 * by default).  The SH-2 reaches this through Vdp1FrameBufferReadWord() when a
 * game reads the framebuffer window.  With the async renderer the SH-2 thread
 * owns no GL context, so the read either decodes a CPU snapshot that the render
 * thread publishes every frame (enabled) or the CPU shadow buffer (disabled;
 * only what the game itself wrote through the CPU -- usually blank). */
extern int yab_vdp1_fb_readback;

/* ---------------------------------------------------------------------------
 * HANGDBG (diagnostic builds only, -DYAB_HANGDBG): stage markers + a watchdog
 * that dumps the machine state when the emulation thread stops advancing.
 * Used to locate machine-side stalls that happen inside a frame (no publish,
 * frontend blocked in the slot wait).  Compiled out by default.
 * ------------------------------------------------------------------------- */
#ifdef YAB_HANGDBG
extern volatile const char *yab_dbg_emu_mark;
extern volatile const char *yab_dbg_front_mark;
void yab_dbg_mark_emu(const char *s);
void yab_dbg_mark_front(const char *s);
void yab_dbg_watchdog_start(void);
void yab_dbg_slot_state(int *cur, int *qfree, int *qready);
void yab_dbg_vdpq_state(int *rcv, int *vdp1, int *ev);
#define YABEMU(s)  yab_dbg_mark_emu(s)
#define YABFRONT(s) yab_dbg_mark_front(s)
#else
#define YABEMU(s)  ((void)0)
#define YABFRONT(s) ((void)0)
#endif

#define CLKTYPE_26MHZ           0
#define CLKTYPE_28MHZ           1

#define VIDEOFORMATTYPE_NTSC    0
#define VIDEOFORMATTYPE_PAL     1

#ifndef NO_CLI
void print_usage(const char *program_name);
#endif

void YabauseChangeTiming(int freqtype);
int YabauseInit(yabauseinit_struct *init);
void YabFlushBackups(void);
void YabauseDeInit(void);
void YabauseSetDecilineMode(int on);
void YabauseResetNoLoad(void);
void YabauseReset(void);
void YabauseResetButton(void);
int YabauseExec(void);
void YabauseStartSlave(void);
void YabauseStopSlave(void);
s64 YabauseGetTicks(void);
void YabauseSetVideoFormat(int type);
void YabauseSpeedySetup(void);
int YabauseQuickLoadGame(void);

#define YABSYS_TIMING_BITS  20
#define YABSYS_TIMING_MASK  ((1 << YABSYS_TIMING_BITS) - 1)

typedef struct
{
   int DecilineMode;
   int DecilineCount;
   int LineCount;
   int VBlankLineCount;
   int MaxLineCount;
   u32 DecilineStop;  // Fixed point
   u32 SH2CycleFrac;  // Fixed point
   u32 DecilineUsec;  // Fixed point
   u32 UsecFrac;      // Fixed point
   int CurSH2FreqType;
   int IsPal;
   u8 UseThreads;
   int NumThreads;
   u8 IsSSH2Running;
   u64 OneFrameTime;
   u64 tickfreq;
   int emulatebios;
   int usequickload;
   int wait_line_count;
   int playing_ssf;
   u32 frame_count;
   int extend_backup;
   u32 sync_shift;
   int use_cpu_affinity;
   int use_sh2_cache;
   int Hcount;
   /* ST-V (Sega Titan Video) -- ported from libretro/yabause@kronos */
   u8 isSTV;
   int stvInputType;
   int isRotated;
} yabsys_struct;

extern yabsys_struct yabsys;

int YabauseEmulate(void);

extern u32 saved_scsp_cycles;
extern volatile u64 saved_m68k_cycles;
#define SCSP_FRACTIONAL_BITS 20
u32 get_cycles_per_line_division(u32 clock, int frames, int lines, int divisions_per_line);
u32 YabauseGetCpuTime();

typedef enum {
  VDP_SETTING_FILTERMODE = 0,
  VDP_SETTING_POLYGON_MODE,
  VDP_SETTING_RESOLUTION_MODE,
  VDP_SETTING_RBG_RESOLUTION_MODE,
  VDP_SETTING_RBG_USE_COMPUTESHADER,
  VDP_SETTING_ROTATE_SCREEN,
  VDP_SETTING_FRAMELIMIT_MODE,
  VDP_SETTING_ASPECT_RATE_MODE
} enSettings;

int VideoSetSetting(int type, int value);

int yprintf( const char * fmt, ... );


int YabauseThread_IsUseBios();
const char * YabauseThread_getBackupPath();
void YabauseThread_setUseBios(int use);
void YabauseThread_setBackupPath(const char * buf);
void YabauseThread_coldBoot();
void YabauseThread_resetPlaymode();

#ifdef __cplusplus
}
#endif

#endif
