#ifndef _MSC_VER
#include <stdbool.h>
#endif
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#ifdef _MSC_VER
#define snprintf _snprintf
#endif

#include <sys/stat.h>

#include <libretro.h>

#include <file/file_path.h>

#include "vdp1.h"
#include "vdp2.h"
#include "peripheral.h"
#include "cdbase.h"
#include "yabause.h"
#include "yui.h"
#include "cheat.h"

#include "cs0.h"
#include "cs2.h"

#include "m68kcore.h"
#include "vidogl.h"
#include "vidsoft.h"
#include "ygl.h"

/* Core options v2 table ported from the reference libretro core
 * (lr-yabasanshiro) -- same keys, defaults, categories and descriptions. */
#include "libretro_core_options.h"

yabauseinit_struct yinit;

static char slash = path_default_slash_c();

static char g_save_dir[PATH_MAX];
static char g_system_dir[PATH_MAX];
static char full_path[PATH_MAX];
static char bios_path[PATH_MAX];
static char bup_path[PATH_MAX];

static int game_width  = 320;
static int game_height = 240;
static int game_interlace;

static int current_width;
static int current_height;

static bool renderer_running = false;
static bool hle_bios_force = false;
static bool one_frame_rendered = false;

static bool libretro_supports_bitmasks = false;
static int16_t libretro_input_bitmask[12] = {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1};
 
#ifdef DYNAREC_DEVMIYAX
static int g_sh2coretype = 3;
#else
static int g_sh2coretype = SH2CORE_INTERPRETER;
#endif

static int g_frame_skip = 1;
static int g_rbg_resolution_mode = 0;
static int g_rbg_use_compute_shader = 1;
static int addon_cart_type = CART_DRAM32MBIT;
static int resolution_mode = 1;
static int initial_resolution_mode = 0;
static int g_resolution_mode = 0;
#ifdef LOW_END
static int max_resolution_mode = 2;
#else
static int max_resolution_mode = 4;
#endif
static int polygon_mode = PERSPECTIVE_CORRECTION;
static int pad_type[12] = {1,1,1,1,1,1,1,1,1,1,1,1};
static int multitap[2] = {0,0};
static unsigned players = 7;

struct retro_perf_callback perf_cb;
retro_get_cpu_features_t perf_get_cpu_features_cb = NULL;

static retro_log_printf_t log_cb;
static retro_video_refresh_t video_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;
static retro_environment_t environ_cb;
static retro_audio_sample_batch_t audio_batch_cb;

#if defined(_USEGLEW_)
static struct retro_hw_render_callback hw_render;
#else
extern struct retro_hw_render_callback hw_render;
#endif

/* defined with the disk-control block further down */
static void disk_register(void);

void retro_set_environment(retro_environment_t cb)
{
   static const struct retro_controller_description peripherals[] = {
       { "Saturn Pad", RETRO_DEVICE_JOYPAD },
       { "Saturn 3D Pad", RETRO_DEVICE_ANALOG },
       { "None", RETRO_DEVICE_NONE },
   };

   static const struct retro_controller_info ports[] = {
      { peripherals, 3 },
      { peripherals, 3 },
      { peripherals, 3 },
      { peripherals, 3 },
      { peripherals, 3 },
      { peripherals, 3 },
      { peripherals, 3 },
      { peripherals, 3 },
      { peripherals, 3 },
      { peripherals, 3 },
      { peripherals, 3 },
      { peripherals, 3 },
      { NULL, 0 },
   };

   environ_cb = cb;

   /* Options come from libretro_core_options.h (v2 with categories; the setter
    * falls back to v1/SET_VARIABLES on older frontends). */
   {
      bool option_cats_supported = false;
      libretro_set_core_options(cb, &option_cats_supported);
   }

   environ_cb(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, (void*)ports);

   /* Multi-disc swap / .m3u playlists (same interface set as the reference core). */
   disk_register();

   /* RetroAchievements: the Saturn work RAM published through set_memory_maps()
    * (HWRAM @0x06000000, LWRAM @0x00200000) is stored byte-swapped the same way
    * Beetle Saturn/Yabause store it, which is what the RA sets were authored
    * against (same statement as the reference core). */
   {
      bool support_achievements = true;
      environ_cb(RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS, &support_achievements);
   }
}
void retro_set_video_refresh(retro_video_refresh_t cb) { video_cb = cb; }
void retro_set_audio_sample(retro_audio_sample_t cb) { (void)cb; }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch_cb = cb; }
void retro_set_input_poll(retro_input_poll_t cb) { input_poll_cb = cb; }
void retro_set_input_state(retro_input_state_t cb) { input_state_cb = cb; }

// PERLIBRETRO
#define PERCORE_LIBRETRO 2

int PERLIBRETROInit(void)
{
   void *controller;

   uint32_t i, j;
   PortData_struct* portdata = NULL;

   //1 multitap + 1 peripherial
   if(multitap[0] == 0 && multitap[1] == 0)
      players = 2;
   else if(multitap[0] == 1 && multitap[1] == 1)
      players = 12;

   PerPortReset();

   for(i = 0; i < players; i++)
   {
      //Ports can handle 6 peripherals, fill port 1 first.
      if((players > 2 && i < 6) || i == 0)
         portdata = &PORTDATA1;
      else
         portdata = &PORTDATA2;

      switch(pad_type[i])
      {
         case RETRO_DEVICE_NONE:
            controller = NULL;
            break;
         case RETRO_DEVICE_ANALOG:
            controller = (void*)Per3DPadAdd(portdata);
            for(j = PERPAD_UP; j <= PERPAD_Z; j++)
               PerSetKey((i << 8) + j, j, controller);
            for(j = PERANALOG_AXIS1; j <= PERANALOG_AXIS7; j++)
               PerSetKey((i << 8) + j, j, controller);
            break;
         case RETRO_DEVICE_JOYPAD:
         default:
            controller = (void*)PerPadAdd(portdata);
            for(j = PERPAD_UP; j <= PERPAD_Z; j++)
               PerSetKey((i << 8) + j, j, controller);
            break;
      }
   }

   return 0;
}

static int input_state_cb_wrapper(unsigned port, unsigned device, unsigned index, unsigned id)
{
   if (libretro_supports_bitmasks && device == RETRO_DEVICE_JOYPAD)
   {
      if (libretro_input_bitmask[port] == -1)
         libretro_input_bitmask[port] = input_state_cb(port, RETRO_DEVICE_JOYPAD, index, RETRO_DEVICE_ID_JOYPAD_MASK);
      return (libretro_input_bitmask[port] & (1 << id));
   }
   else
      return input_state_cb(port, device, index, id);
}

static int PERLIBRETROHandleEvents(void)
{
   unsigned i = 0;

   input_poll_cb();

   for(i = 0; i < players; i++)
   {
         int analog_left_x = 0;
         int analog_left_y = 0;
         int analog_right_x = 0;
         int analog_right_y = 0;
         uint16_t l_trigger, r_trigger;
         libretro_input_bitmask[i] = -1;

         switch(pad_type[i])
         {
            case RETRO_DEVICE_ANALOG:
               analog_left_x = input_state_cb_wrapper(i, RETRO_DEVICE_ANALOG,
                     RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X);

               PerAxisValue((i << 8) + PERANALOG_AXIS1, (u8)((analog_left_x + 0x8000) >> 8));

               analog_left_y = input_state_cb_wrapper(i, RETRO_DEVICE_ANALOG,
                     RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y);

               PerAxisValue((i << 8) + PERANALOG_AXIS2, (u8)((analog_left_y + 0x8000) >> 8));

               // analog triggers
               l_trigger = input_state_cb_wrapper( i, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_BUTTON, RETRO_DEVICE_ID_JOYPAD_L2 );
               r_trigger = input_state_cb_wrapper( i, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_BUTTON, RETRO_DEVICE_ID_JOYPAD_R2 );

               // if no analog trigger support, use digital
               if (l_trigger == 0)
                  l_trigger = input_state_cb_wrapper( i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2 ) ? 0x7FFF : 0;
               if (r_trigger == 0)
                  r_trigger = input_state_cb_wrapper( i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2 ) ? 0x7FFF : 0;

               PerAxisValue((i << 8) + PERANALOG_AXIS3, (u8)((r_trigger > 0 ? r_trigger + 0x8000 : 0) >> 8));
               PerAxisValue((i << 8) + PERANALOG_AXIS4, (u8)((l_trigger > 0 ? l_trigger + 0x8000 : 0) >> 8));

            case RETRO_DEVICE_JOYPAD:

               if (input_state_cb_wrapper(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP))
                  PerKeyDown((i << 8) + PERPAD_UP);
               else
                  PerKeyUp((i << 8) + PERPAD_UP);
               if (input_state_cb_wrapper(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN))
                  PerKeyDown((i << 8) + PERPAD_DOWN);
               else
                  PerKeyUp((i << 8) + PERPAD_DOWN);
               if (input_state_cb_wrapper(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT))
                  PerKeyDown((i << 8) + PERPAD_LEFT);
               else
                  PerKeyUp((i << 8) + PERPAD_LEFT);
               if (input_state_cb_wrapper(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT))
                  PerKeyDown((i << 8) + PERPAD_RIGHT);
               else
                  PerKeyUp((i << 8) + PERPAD_RIGHT);

               if (input_state_cb_wrapper(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y))
                  PerKeyDown((i << 8) + PERPAD_X);
               else
                  PerKeyUp((i << 8) + PERPAD_X);

               if (input_state_cb_wrapper(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B))
                  PerKeyDown((i << 8) + PERPAD_A);
               else
                  PerKeyUp((i << 8) + PERPAD_A);

               if (input_state_cb_wrapper(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A))
                  PerKeyDown((i << 8) + PERPAD_B);
               else
                  PerKeyUp((i << 8) + PERPAD_B);

               if (input_state_cb_wrapper(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X))
                  PerKeyDown((i << 8) + PERPAD_Y);
               else
                  PerKeyUp((i << 8) + PERPAD_Y);

               if (input_state_cb_wrapper(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L))
                  PerKeyDown((i << 8) + PERPAD_C);
               else
                  PerKeyUp((i << 8) + PERPAD_C);

               if (input_state_cb_wrapper(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R))
                  PerKeyDown((i << 8) + PERPAD_Z);
               else
                  PerKeyUp((i << 8) + PERPAD_Z);

               if (input_state_cb_wrapper(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START))
                  PerKeyDown((i << 8) + PERPAD_START);
               else
                  PerKeyUp((i << 8) + PERPAD_START);

               if (input_state_cb_wrapper(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2))
                  PerKeyDown((i << 8) + PERPAD_LEFT_TRIGGER);
               else
                  PerKeyUp((i << 8) + PERPAD_LEFT_TRIGGER);

               if (input_state_cb_wrapper(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2))
                  PerKeyDown((i << 8) + PERPAD_RIGHT_TRIGGER);
               else
                  PerKeyUp((i << 8) + PERPAD_RIGHT_TRIGGER);
               break;

            default:
               break;
         }
   }

   if ( YabauseExec() != 0 )
      return -1;
   return 0;
}

void PERLIBRETRODeInit(void) {}

void PERLIBRETRONothing(void) {}

u32 PERLIBRETROScan(u32 flags) { return 0;}

void PERLIBRETROKeyName(u32 key, char *name, int size) {}

PerInterface_struct PERLIBRETROJoy = {
    PERCORE_LIBRETRO,
    "Libretro Input Interface",
    PERLIBRETROInit,
    PERLIBRETRODeInit,
    PERLIBRETROHandleEvents,
    PERLIBRETROScan,
    0,
    PERLIBRETRONothing,
    PERLIBRETROKeyName
};

// SNDLIBRETRO
#define SNDCORE_LIBRETRO   11
#define SAMPLERATE         44100

static u32 audio_size;
static u32 soundlen;
static u32 soundbufsize;
static s16 *sound_buf;

static int SNDLIBRETROInit(void) {
    int vertfreq = (yabsys.IsPal == 1 ? 50 : 60);
    soundlen = (SAMPLERATE * 100 + (vertfreq >> 1)) / vertfreq;
    soundbufsize = (soundlen<<2 * sizeof(s16));
    if ((sound_buf = (s16 *)malloc(soundbufsize)) == NULL)
        return -1;
    memset(sound_buf, 0, soundbufsize);
    return 0;
}

static void SNDLIBRETRODeInit(void) {
   if (sound_buf)
      free(sound_buf);
}

static int SNDLIBRETROReset(void) { return 0; }

static int SNDLIBRETROChangeVideoFormat(int vertfreq)
{
    soundlen = (SAMPLERATE * 100 + (vertfreq >> 1)) / vertfreq;
    soundbufsize = (soundlen<<2 * sizeof(s16));
    if (sound_buf)
        free(sound_buf);
    if ((sound_buf = (s16 *)malloc(soundbufsize)) == NULL)
        return -1;
    memset(sound_buf, 0, soundbufsize);
    return 0;
}

static void sdlConvert32uto16s(int32_t *srcL, int32_t *srcR, int16_t *dst, size_t len)
{
   u32 i;

   for (i = 0; i < len; i++)
   {
      // Left Channel
      if (*srcL > 0x7FFF)
         *dst = 0x7FFF;
      else if (*srcL < -0x8000)
         *dst = -0x8000;
      else
         *dst = *srcL;
      srcL++;
      dst++;

      // Right Channel
      if (*srcR > 0x7FFF)
         *dst = 0x7FFF;
      else if (*srcR < -0x8000)
         *dst = -0x8000;
      else
         *dst = *srcR;
      srcR++;
      dst++;
   }
}

static void SNDLIBRETROUpdateAudio(u32 *leftchanbuffer, u32 *rightchanbuffer, u32 num_samples)
{
   sdlConvert32uto16s((int32_t*)leftchanbuffer, (int32_t*)rightchanbuffer, sound_buf, num_samples);
   audio_batch_cb(sound_buf, num_samples);

   audio_size -= num_samples;
}

static u32 SNDLIBRETROGetAudioSpace(void) { return audio_size; }

void SNDLIBRETROMuteAudio(void) {}

void SNDLIBRETROUnMuteAudio(void) {}

void SNDLIBRETROSetVolume(int volume) {}

SoundInterface_struct SNDLIBRETRO = {
    SNDCORE_LIBRETRO,
    "Libretro Sound Interface",
    SNDLIBRETROInit,
    SNDLIBRETRODeInit,
    SNDLIBRETROReset,
    SNDLIBRETROChangeVideoFormat,
    SNDLIBRETROUpdateAudio,
    SNDLIBRETROGetAudioSpace,
    SNDLIBRETROMuteAudio,
    SNDLIBRETROUnMuteAudio,
    SNDLIBRETROSetVolume
};

M68K_struct *M68KCoreList[] = {
    &M68KDummy,
#ifdef HAVE_MUSASHI
    &M68KMusashi,
#else
    &M68KC68K,
#endif
    NULL
};

SH2Interface_struct *SH2CoreList[] = {
    &SH2Interpreter,
    &SH2DebugInterpreter,
#ifdef DYNAREC_DEVMIYAX
    &SH2Dyn,
#endif
    NULL
};

PerInterface_struct *PERCoreList[] = {
    &PERDummy,
    &PERLIBRETROJoy,
    NULL
};

CDInterface *CDCoreList[] = {
    &DummyCD,
    &ISOCD,
    NULL
};

SoundInterface_struct *SNDCoreList[] = {
    &SNDDummy,
    &SNDLIBRETRO,
    NULL
};

VideoInterface_struct *VIDCoreList[] = {
    //&VIDDummy,
    &VIDOGL,
    &VIDSoft,
    NULL
};

#pragma mark Yabause Callbacks

void YuiMsg(const char *format, ...)
{
  char buf[512]; 
  va_list arglist;
  va_start( arglist, format );
  int rc = vsnprintf(buf, 512, format, arglist);
  va_end( arglist );
  log_cb(RETRO_LOG_INFO, buf);
}

void YuiErrorMsg(const char *string)
{
   if (log_cb)
      log_cb(RETRO_LOG_ERROR, "Yabause: %s\n", string);
}

#if defined(YAB_CORE_SHARED_CONTEXT)
/* ============ core-side shared GL context for the async VDP thread ==========
 * The async VDP thread issues real GL calls, so it needs a current context.
 * The frontend's context belongs to the frontend's thread and cannot be handed
 * over (a libretro frontend also draws with it: minarch-gl's menu captures the
 * frame from its own FBO -- stealing the context makes that fail with
 * "capture FBO incomplete" and then SIGSEGV).  Device-verified on this libmali:
 *   - a third party inside the frontend process can read the frontend's
 *     EGLDisplay/EGLContext and create a context SHARING with it (pbuffer),
 *   - textures are shared between the two contexts, FBOs are NOT,
 *   - so the renderer targets a core-created "mirror" FBO that attaches the
 *     FRONTEND's colour texture: what the VDP thread draws lands in the texture
 *     the frontend presents (zero copy, hw-render contract unchanged).
 * The shared context is made current on the emulation thread BEFORE
 * YabauseInit(), so every GL object the engine creates (its internal FBOs
 * included) lives in the context the VDP thread later uses.
 * Frame delivery: YuiSwapBuffers() runs on the VDP thread, but the frontend's
 * video_refresh does GL (shader chain + swap) and must run on the frontend's
 * thread, so the port only marks the frame there and retro_run() issues
 * video_cb().
 * ========================================================================== */
#include <dlfcn.h>

typedef void *YK_Display; typedef void *YK_Context; typedef void *YK_Surface;
typedef void *YK_Config;  typedef unsigned int YK_Uint; typedef int YK_Int;
typedef unsigned int YK_Enum; typedef unsigned int YK_Bool;

static struct {
   void *lib;
   YK_Display dpy; YK_Context ctx_front; YK_Context ctx_sub; YK_Surface pb;
   YK_Surface draw, read;
   YK_Config cfg; YK_Int cfgid;
   YK_Uint front_fbo, front_tex, mirror_fbo;
   int state;                    /* 0 = untried, -1 = failed, 1 = ready */
   volatile int frame_pending, res_pending;
   int worker_valid; pthread_t worker; pthread_t main_thread;
   /* What the frontend's thread had current before the core borrowed ctx_sub
    * (see yk_remember_front/yk_restore_front). Only ever touched on the
    * frontend's thread; the VDP thread owns ctx_sub for its lifetime. */
   YK_Context prev_ctx; YK_Surface prev_draw, prev_read;
   YK_Display (*GetCurrentDisplay)(void);
   YK_Context (*GetCurrentContext)(void);
   YK_Surface (*GetCurrentSurface)(int);
   YK_Bool (*QueryContext)(YK_Display, YK_Context, YK_Int, YK_Int *);
   YK_Bool (*GetConfigs)(YK_Display, YK_Config *, YK_Int, YK_Int *);
   YK_Bool (*GetConfigAttrib)(YK_Display, YK_Config, YK_Int, YK_Int *);
   YK_Surface (*CreatePbufferSurface)(YK_Display, YK_Config, const YK_Int *);
   YK_Context (*CreateContext)(YK_Display, YK_Config, YK_Context, const YK_Int *);
   YK_Bool (*MakeCurrent)(YK_Display, YK_Surface, YK_Surface, YK_Context);
   void *(*GetProcAddress)(const char *);
   void (*GenFramebuffers)(YK_Int, YK_Uint *);
   void (*BindFramebuffer)(YK_Enum, YK_Uint);
   void (*FramebufferTexture2D)(YK_Enum, YK_Enum, YK_Enum, YK_Uint, YK_Int);
   YK_Enum (*CheckFramebufferStatus)(YK_Enum);
   void (*GetFramebufferAttachmentParameteriv)(YK_Enum, YK_Enum, YK_Enum, YK_Int *);
   void (*GetIntegerv)(YK_Enum, YK_Int *);
   void (*Finish)(void);
} yk;

#define YK_EGL_NONE              0x3038
#define YK_EGL_CONFIG_ID         0x3028
#define YK_EGL_WIDTH             0x3057
#define YK_EGL_HEIGHT            0x3056
#define YK_EGL_CONTEXT_CLIENT_VERSION 0x3098
#define YK_EGL_DRAW              0x3059
#define YK_EGL_READ              0x305A
#define YK_GL_FRAMEBUFFER        0x8D40
#define YK_GL_COLOR_ATTACHMENT0  0x8CE0
#define YK_GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE 0x8CD0
#define YK_GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME 0x8CD1
#define YK_GL_TEXTURE            0x1702
#define YK_GL_TEXTURE_2D         0x0DE1
#define YK_GL_FRAMEBUFFER_BINDING 0x8CA6
#define YK_GL_FRAMEBUFFER_COMPLETE 0x8CD5

/* Context-handoff trace: build with -DYAB_CTX_TRACE to see, on every borrow /
 * hand-back, which EGL context really ends up current on which thread.  The
 * hi-res crash is a resolution-switch (res_pending) handover, so this is the
 * cheap way to tell "engine GL ran in the frontend's context" (yk_attach
 * failed) apart from "engine GL ran in the right one". */
#if defined(YAB_CTX_TRACE)
#define YK_TRACE(...) yk_log("[CTX] " __VA_ARGS__)
#else
#define YK_TRACE(...) ((void)0)
#endif

static void yk_log(const char *fmt, ...)
{
   char buf[512];
   va_list ap;
   va_start(ap, fmt);
   vsnprintf(buf, sizeof(buf), fmt, ap);
   va_end(ap);
   if (log_cb)
      log_cb(RETRO_LOG_INFO, "[YK] %s\n", buf);
   else
      printf("[YK] %s\n", buf);
   fflush(stdout);
}

/* Emulation thread, frontend context current (called from context_reset, i.e.
 * after the frontend created its FBO).  Creates the shared context, the mirror
 * FBO, and leaves the SHARED context current on this thread so that
 * YabauseInit()/VIDOGLInit() create the engine's GL objects here. */
static void yk_load(void)
{
   YK_Config cfgs[64];
   YK_Int ncfg = 0, i, prev_fbo = 0, type = -1, name = 0;
   YK_Int pbattr[5], ctxattr[5];
   YK_Enum st;

   if (yk.state)
      return;
   yk.state = -1;
   yk.main_thread = pthread_self();

   yk.lib = dlopen("libEGL.so.1", RTLD_NOW | RTLD_LOCAL);
   if (!yk.lib)
      yk.lib = dlopen("libmali.so.0", RTLD_NOW | RTLD_LOCAL);
   if (!yk.lib) { yk_log("no EGL library"); return; }

   yk.GetCurrentDisplay = dlsym(yk.lib, "eglGetCurrentDisplay");
   yk.GetCurrentContext = dlsym(yk.lib, "eglGetCurrentContext");
   yk.GetCurrentSurface = dlsym(yk.lib, "eglGetCurrentSurface");
   yk.QueryContext      = dlsym(yk.lib, "eglQueryContext");
   yk.GetConfigs        = dlsym(yk.lib, "eglGetConfigs");
   yk.GetConfigAttrib   = dlsym(yk.lib, "eglGetConfigAttrib");
   yk.CreatePbufferSurface = dlsym(yk.lib, "eglCreatePbufferSurface");
   yk.CreateContext     = dlsym(yk.lib, "eglCreateContext");
   yk.MakeCurrent       = dlsym(yk.lib, "eglMakeCurrent");
   yk.GetProcAddress    = dlsym(yk.lib, "eglGetProcAddress");
   if (!yk.GetCurrentDisplay || !yk.GetCurrentContext || !yk.GetCurrentSurface ||
       !yk.QueryContext || !yk.GetConfigs || !yk.GetConfigAttrib ||
       !yk.CreatePbufferSurface || !yk.CreateContext || !yk.MakeCurrent ||
       !yk.GetProcAddress)
   { yk_log("missing EGL entry points"); return; }

   yk.dpy       = yk.GetCurrentDisplay();
   yk.ctx_front = yk.GetCurrentContext();
   yk.draw      = yk.GetCurrentSurface(YK_EGL_DRAW);
   yk.read      = yk.GetCurrentSurface(YK_EGL_READ);
   /* The frontend's context is the one current here (yk_load() is called from
    * context_reset with the frontend's context current): seed the hand-back
    * record from it, since yk_remember_front() cannot run before the entry
    * points above exist. */
   yk.prev_ctx  = yk.ctx_front;
   yk.prev_draw = yk.draw;
   yk.prev_read = yk.read;
   if (!yk.dpy || !yk.ctx_front)
   { yk_log("frontend has no current EGL context"); return; }

   if (!yk.QueryContext(yk.dpy, yk.ctx_front, YK_EGL_CONFIG_ID, &yk.cfgid))
   { yk_log("eglQueryContext(CONFIG_ID) failed"); return; }
   if (yk.GetConfigs(yk.dpy, cfgs, 64, &ncfg))
   {
      for (i = 0; i < ncfg; i++)
      {
         YK_Int id = -1;
         if (yk.GetConfigAttrib(yk.dpy, cfgs[i], YK_EGL_CONFIG_ID, &id) && id == yk.cfgid)
            yk.cfg = cfgs[i];
      }
   }
   if (!yk.cfg) { yk_log("frontend EGLConfig not found"); return; }

   /* The frontend's colour texture: query it while ITS context is current. */
   yk.GenFramebuffers     = yk.GetProcAddress("glGenFramebuffers");
   yk.BindFramebuffer     = yk.GetProcAddress("glBindFramebuffer");
   yk.FramebufferTexture2D = yk.GetProcAddress("glFramebufferTexture2D");
   yk.CheckFramebufferStatus = yk.GetProcAddress("glCheckFramebufferStatus");
   yk.GetFramebufferAttachmentParameteriv =
         yk.GetProcAddress("glGetFramebufferAttachmentParameteriv");
   yk.GetIntegerv         = yk.GetProcAddress("glGetIntegerv");
   yk.Finish              = yk.GetProcAddress("glFinish");

   yk.front_fbo = (YK_Uint)(uintptr_t)hw_render.get_current_framebuffer();
   if (yk.GetIntegerv)
      yk.GetIntegerv(YK_GL_FRAMEBUFFER_BINDING, &prev_fbo);
   if (yk.front_fbo && yk.BindFramebuffer && yk.GetFramebufferAttachmentParameteriv)
   {
      yk.BindFramebuffer(YK_GL_FRAMEBUFFER, yk.front_fbo);
      yk.GetFramebufferAttachmentParameteriv(YK_GL_FRAMEBUFFER, YK_GL_COLOR_ATTACHMENT0,
            YK_GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &type);
      if (type == YK_GL_TEXTURE)
         yk.GetFramebufferAttachmentParameteriv(YK_GL_FRAMEBUFFER, YK_GL_COLOR_ATTACHMENT0,
               YK_GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &name);
      yk.BindFramebuffer(YK_GL_FRAMEBUFFER, (YK_Uint)prev_fbo);
      yk.front_tex = (YK_Uint)name;
   }
   if (!yk.front_tex)
   { yk_log("frontend FBO has no colour texture -> async rendering disabled"); return; }

   pbattr[0] = YK_EGL_WIDTH;  pbattr[1] = 16;
   pbattr[2] = YK_EGL_HEIGHT; pbattr[3] = 16;
   pbattr[4] = YK_EGL_NONE;
   yk.pb = yk.CreatePbufferSurface(yk.dpy, yk.cfg, pbattr);
   if (!yk.pb) { yk_log("eglCreatePbufferSurface failed"); return; }

   ctxattr[0] = YK_EGL_CONTEXT_CLIENT_VERSION; ctxattr[1] = 3;
   ctxattr[2] = YK_EGL_NONE;
   yk.ctx_sub = yk.CreateContext(yk.dpy, yk.cfg, yk.ctx_front, ctxattr);
   if (!yk.ctx_sub)
   {
      ctxattr[1] = 2;
      yk.ctx_sub = yk.CreateContext(yk.dpy, yk.cfg, yk.ctx_front, ctxattr);
   }
   if (!yk.ctx_sub) { yk_log("eglCreateContext(share) failed"); return; }

   /* KEY: the engine (YabauseInit -> VIDOGLInit -> YglInit) must create its GL
    * objects in THIS context -- FBOs are not shared across contexts, so objects
    * built in the frontend's context are invalid names for the VDP thread. */
   if (!yk.MakeCurrent(yk.dpy, yk.pb, yk.pb, yk.ctx_sub))
   { yk_log("eglMakeCurrent(share) on emulation thread failed"); return; }

   yk.GenFramebuffers(1, &yk.mirror_fbo);
   yk.BindFramebuffer(YK_GL_FRAMEBUFFER, yk.mirror_fbo);
   yk.FramebufferTexture2D(YK_GL_FRAMEBUFFER, YK_GL_COLOR_ATTACHMENT0,
         YK_GL_TEXTURE_2D, yk.front_tex, 0);
   st = yk.CheckFramebufferStatus ? yk.CheckFramebufferStatus(YK_GL_FRAMEBUFFER) : 0;
   yk_log("share ctx=%p pbuffer=%p front_ctx=%p cfgid=%d front_fbo=%u front_tex=%u "
          "mirror_fbo=%u status=0x%x",
          yk.ctx_sub, yk.pb, yk.ctx_front, (int)yk.cfgid, yk.front_fbo, yk.front_tex,
          yk.mirror_fbo, (unsigned)st);
   if (yk.CheckFramebufferStatus && st != YK_GL_FRAMEBUFFER_COMPLETE)
   { yk_log("mirror FBO incomplete -> async rendering disabled"); return; }

   yk.state = 1;
}

/* Make the shared context current on the calling thread (VDP thread: for its
 * lifetime; emulation thread: only while it borrows the context for engine GL
 * work, e.g. a resolution/option change). */
static void yk_remember_front(void);
static void yk_restore_front(void);

static int yk_attach(void)
{
   if (yk.state != 1)
      return 0;
   /* NB: do NOT use pthread_equal() here.  In this cross toolchain/glibc combo
    * it returns 0 for two identical pthread_t values (observed: eq=0 with
    * me==worker), which silently disables the frame-delivery path.  Compare the
    * values directly (glibc's pthread_equal is exactly this comparison). */
   if (!yk.worker_valid && (pthread_self() != yk.main_thread))
   {
      yk.worker = pthread_self();
      yk.worker_valid = 1;
   }
   /* A borrower that is not the VDP thread (i.e. the frontend's thread) must
    * remember what it is taking the thread away from, so yk_detach() can hand
    * it back instead of leaving the thread context-less. */
   if (!(yk.worker_valid && pthread_self() == yk.worker))
      yk_remember_front();
   if (yk.GetCurrentContext && yk.GetCurrentContext() == yk.ctx_sub)
   {
      YK_TRACE("attach tid=%lu: already sub", (unsigned long)pthread_self());
      return 0;                 /* already ours on this thread */
   }
   {
      YK_Bool ok = yk.MakeCurrent(yk.dpy, yk.pb, yk.pb, yk.ctx_sub);
      YK_TRACE("attach tid=%lu rc=%d now=%p sub=%p front=%p", (unsigned long)pthread_self(),
            (int)ok,
            (void *)(yk.GetCurrentContext ? yk.GetCurrentContext() : NULL),
            (void *)yk.ctx_sub, (void *)yk.ctx_front);
      return ok ? 0 : -1;
   }
}

/* Remember the frontend's context (and the surfaces it has current) so it can
 * be handed back later.  Called whenever the frontend's context is the one
 * current on this thread; a no-op while the core's own context is current or
 * before yk_load() has resolved the EGL entry points. */
static void yk_remember_front(void)
{
   YK_Context cur;
   if (!yk.GetCurrentContext)
      return;
   cur = yk.GetCurrentContext();
   if (!cur || cur == yk.ctx_sub)
      return;
   yk.prev_ctx  = cur;
   yk.prev_draw = yk.GetCurrentSurface ? yk.GetCurrentSurface(YK_EGL_DRAW) : NULL;
   yk.prev_read = yk.GetCurrentSurface ? yk.GetCurrentSurface(YK_EGL_READ) : NULL;
}

/* Hand the calling thread its own context back.
 *
 * Why this matters: the frontend (SDL) keeps a PER-THREAD record of which
 * context it made current and treats "the context I would make current is
 * already the one I recorded" as a no-op.  Releasing the borrowing thread to
 * EGL_NO_CONTEXT behind SDL's back therefore leaves the frontend believing its
 * context is current while the thread really has none, so every frontend GL
 * call of that frame is silently void -- measured on device as
 * "present shader compile failed" with an uninitialised info-log one frame
 * before the retry succeeded, and in the offscreen harness (with the frontend's
 * unconditional eglMakeCurrent disabled) as a permanently context-less frontend
 * thread whose every FBO readback comes back black.  Restoring the saved
 * context keeps that record true, and keeps the frontend's FBO/texture
 * namespace (names 1/3, the same names the engine's own objects use) from ever
 * being touched by the wrong context.
 *
 * Unconditional on purpose: if this thread already holds a context that is not
 * ours there is nothing to give back; if it holds ctx_sub (the normal case) or
 * nothing at all (the VDP thread took ctx_sub over while the engine was
 * starting) the frontend's context is the right thing to have current.
 */
static void yk_restore_front(void)
{
   YK_Context cur, back;
   if (!yk.GetCurrentContext || !yk.MakeCurrent)
      return;
   cur = yk.GetCurrentContext();
   if (cur && cur != yk.ctx_sub)
      return;                   /* somebody else's context is current here */
   back = yk.prev_ctx ? yk.prev_ctx : yk.ctx_front;
   if (!back)
      return;
   {
      YK_Bool ok = yk.MakeCurrent(yk.dpy,
            yk.prev_ctx ? yk.prev_draw : yk.draw,
            yk.prev_ctx ? yk.prev_read : yk.read,
            back);
      YK_TRACE("restore-front tid=%lu cur_was=%p back=%p rc=%d now=%p",
            (unsigned long)pthread_self(), (void *)cur, (void *)back, (int)ok,
            (void *)(yk.GetCurrentContext ? yk.GetCurrentContext() : NULL));
   }
}

static void yk_detach(void)
{
   if (yk.state != 1)
      return;
   if (yk.worker_valid && pthread_self() == yk.worker)
   {
      if (yk.GetCurrentContext && yk.GetCurrentContext() != yk.ctx_sub)
         return;                /* not ours on this thread */
      yk.MakeCurrent(yk.dpy, NULL, NULL, NULL);   /* VDP thread: plain release */
      YK_TRACE("detach[worker] tid=%lu -> released (now=%p)",
            (unsigned long)pthread_self(),
            (void *)(yk.GetCurrentContext ? yk.GetCurrentContext() : NULL));
      return;
   }
   /* Frontend thread: hand back the context this thread owns (no-op when it is
    * already current, which is also what makes a failed yk_attach() harmless). */
   yk_restore_front();
}

/* Does the calling thread currently hold the shared context? */
static int yk_held(void)
{
   return yk.state == 1 && yk.GetCurrentContext && yk.GetCurrentContext() == yk.ctx_sub;
}
#endif /* YAB_CORE_SHARED_CONTEXT */


static int first_ctx_reset = 1;

int YuiUseOGLOnThisThread()
{
#if defined(YAB_CORE_SHARED_CONTEXT)
  return yk_attach();
#elif !defined(_USEGLEW_)
  return glsm_ctl(GLSM_CTL_STATE_BIND, NULL);
#endif
}

int YuiRevokeOGLOnThisThread()
{
#if defined(YAB_CORE_SHARED_CONTEXT)
  yk_detach();
  return 0;
#elif !defined(_USEGLEW_)
  return glsm_ctl(GLSM_CTL_STATE_UNBIND, NULL);
#endif
}

int YuiGetFB(void)
{
#if defined(YAB_CORE_SHARED_CONTEXT)
  /* the engine renders into the core's mirror FBO, which attaches the
   * frontend's colour texture */
  if (yk.mirror_fbo)
     return (int)yk.mirror_fbo;
#endif
  return hw_render.get_current_framebuffer();
}

void retro_reinit_av_info(void)
{
    struct retro_system_av_info av_info;
    retro_get_system_av_info(&av_info);
    environ_cb(RETRO_ENVIRONMENT_SET_GEOMETRY, &av_info);
}

void retro_set_resolution()
{
   // If resolution_mode > initial_resolution_mode, we'll need a restart to reallocate the max size for buffer
   if (resolution_mode > initial_resolution_mode)
   {
      log_cb(RETRO_LOG_INFO, "Restart the core for x%d resolution\n", resolution_mode);
      resolution_mode = initial_resolution_mode;
   }
   // Downscale resolution_mode for Hi-Res games
   if (game_height > 256 && resolution_mode > max_resolution_mode/2)
   {
      log_cb(RETRO_LOG_INFO, "Halving Hi-Res games resolution mode\n", resolution_mode);
      resolution_mode = max_resolution_mode/2;
   }
   switch(resolution_mode)
   {
      case 1:
         /* "original" -> RES_NATIVE (0): draw straight into the framebuffer the
          * frontend gave us (YuiGetFB(), i.e. the mirror FBO over its colour
          * texture).  RES_ORIGINAL (3) instead renders into a game-sized
          * sub-render-target and blits/upscales it every frame: on a hi-res game
          * (e.g. Virtua Fighter 2 at 704x480) that costs an extra target + blit
          * per frame, and the mode switch on the BIOS->game handover reallocates
          * those targets mid-stream.  Aligned with lr-yabasanshiro's libretro
          * frontend, which picks native for the same reason (its comment: the
          * upscale path is broken under the libretro HW context). */
         g_resolution_mode = RES_NATIVE;
         break;
      case 2:
         g_resolution_mode = 2;
         break;
      case 4:
         g_resolution_mode = 1;
         break;
   }
   current_width = game_width * resolution_mode;
   current_height = game_height * resolution_mode;
   /* this tree's Resize() takes the aspect-rate mode as a 6th argument
    * (ASPECT_RATE_MODE: ORIGINAL=0, _4_3, _16_9, FULL=3); the reference core
    * passes ORIGINAL here, so match it. */
   VIDCore->Resize(0, 0, current_width, current_height, 0, ORIGINAL);
   retro_reinit_av_info();
   VIDCore->SetSettingValue(VDP_SETTING_RESOLUTION_MODE, g_resolution_mode);
}

void YuiSwapBuffers(void)
{
   int prev_game_width = game_width;
   int prev_game_height = game_height;
   VIDCore->GetNativeResolution(&game_width, &game_height, &game_interlace);
   if ((prev_game_width != game_width) || (prev_game_height != game_height))
   {
#if defined(YAB_CORE_SHARED_CONTEXT)
      if (yk.state == 1 && yk.worker_valid && (pthread_self() == yk.worker))
      {
         /* VIDCore->Resize() + the AV-info env callback must run on the
          * frontend's thread (retro_run); block hashing/GL here would also
          * fight the VDP thread for the shared context.  Defer it. */
         yk.res_pending = 1;
      }
      else
#endif
         retro_set_resolution();
   }
   audio_size = soundlen;
#if defined(YAB_CORE_SHARED_CONTEXT)
   if (yk.state == 1 && yk.worker_valid && (pthread_self() == yk.worker))
   {
      /* video_cb -> frontend video_refresh -> shader chain + swap: those run
       * with the FRONTEND's context on the FRONTEND's thread, so only mark
       * the frame here and let retro_run() deliver it.  Finish first: the
       * frontend samples the texture this thread just rendered into. */
      if (yk.Finish)
         yk.Finish();
      yk.frame_pending = 1;
      one_frame_rendered = true;
      return;
   }
#endif
   video_cb(RETRO_HW_FRAME_BUFFER_VALID, current_width, current_height, 0);
   one_frame_rendered = true;
}

/* ---------------------------------------------------------------------------
 * Frontend-visible memory: backup RAM (.srm / RetroAchievements) and the work
 * RAM layout.  Ported from lr-yabasanshiro src/libretro/libretro.c:1700-1793.
 *
 * The frontend may write its .srm into the buffer returned by
 * retro_get_memory_data(RETRO_MEMORY_SAVE_RAM) before the core's own BupRam
 * exists, so expose a shadow buffer, seed it from the core's backup.bin, push it
 * into BupRam once that exists, then mirror it back out every frame.  The core
 * keeps writing its own backup.bin as well, so saves persist either way.
 * ------------------------------------------------------------------------- */
#define YABA_INTERNAL_BUP_SIZE 0x10000

static u8  sram_shadow[YABA_INTERNAL_BUP_SIZE];
static int sram_apply_pending = 0; /* push shadow -> BupRam once BupRam exists */

static void sram_seed_from_backup(const char *path)
{
   FILE *bf = fopen(path, "rb");
   if (bf)
   {
      size_t n = fread(sram_shadow, 1, sizeof(sram_shadow), bf);
      fclose(bf);
      if (n < sizeof(sram_shadow))
         FormatBackupRam(sram_shadow, sizeof(sram_shadow));
   }
   else
      FormatBackupRam(sram_shadow, sizeof(sram_shadow));
   sram_apply_pending = 1;
}

static void sram_sync(void)
{
   if (!BupRam)
      return;
   if (sram_apply_pending)
   {
      memcpy(BupRam, sram_shadow, sizeof(sram_shadow));
      sram_apply_pending = 0;
   }
   else
      memcpy(sram_shadow, BupRam, sizeof(sram_shadow));
}

/* Publish the Saturn work-RAM layout so the frontend (RetroAchievements, cheat
 * search, ...) can address it.  Called from context_reset() once YabauseInit()
 * has allocated the buffers. */
static void set_memory_maps(void)
{
   static struct retro_memory_descriptor descs[2];
   struct retro_memory_map mmap;

   if (!HighWram || !LowWram)
      return;

   memset(descs, 0, sizeof(descs));

   /* High Work RAM -- HWRAM @ 0x06000000, 1 MB (the primary region). */
   descs[0].flags     = RETRO_MEMDESC_SYSTEM_RAM;
   descs[0].ptr       = HighWram;
   descs[0].start     = 0x06000000;
   descs[0].len       = 0x100000;
   descs[0].addrspace = "HWRAM";

   /* Low Work RAM -- LWRAM @ 0x00200000, 1 MB. */
   descs[1].flags     = RETRO_MEMDESC_SYSTEM_RAM;
   descs[1].ptr       = LowWram;
   descs[1].start     = 0x00200000;
   descs[1].len       = 0x100000;
   descs[1].addrspace = "LWRAM";

   mmap.descriptors     = descs;
   mmap.num_descriptors = 2;
   environ_cb(RETRO_ENVIRONMENT_SET_MEMORY_MAPS, &mmap);
}

static void context_reset(void)
{
#if defined(YAB_CORE_SHARED_CONTEXT)
   /* Create the core's own (shared) context and leave it current on this
    * thread, so YabauseInit()'s GL objects live in the context the VDP
    * thread uses.  Must happen before YabauseInit() starts that thread.
    * yk_remember_front() first, and yk_restore_front() at the end: the engine's GL
    * work below is the only part of this call that must run in the core's
    * context -- handing the frontend's context back afterwards is what keeps
    * the frontend thread's notion of "which context is current" true (see
    * yk_restore_front).  yk_attach() re-borrows ctx_sub on a repeat reset,
    * where yk_load() is a no-op. */
   /* If the core's context is already current on this thread, an OUTER borrow
    * (retro_run's option/res_pending bracket, or apply_state_buffer) is in
    * progress and owns the hand-back -- this re-entry (MA_GL_update_fbo_size ->
    * context_reset) must not hand the frontend's context back early. */
   int outer_borrow = yk_held();
   YK_TRACE("context_reset enter outer_borrow=%d now=%p", outer_borrow,
         (void *)(yk.GetCurrentContext ? yk.GetCurrentContext() : NULL));
   if (!outer_borrow)
      yk_remember_front();
   yk_load();
   yk_attach();
#endif
#if !defined(_USEGLEW_)
   glsm_ctl(GLSM_CTL_STATE_CONTEXT_RESET, NULL);
   glsm_ctl(GLSM_CTL_STATE_SETUP, NULL);
#endif
#if defined(_OGLES3_)
   /* glsym_es3.c's symbol table has no ES 3.1 entries, so the rglgen pass above
    * leaves glBindImageTexture()/glDispatchCompute()/glMemoryBarrier() NULL and
    * the first game that takes RBGGenerator's compute path (e.g. Virtua
    * Fighter 2's title screen) jumps to NULL: si_code=SEGV_MAPERR, si_addr=0,
    * PC=0.  Resolve them here, and if the driver cannot provide them, keep the
    * engine on the fragment-shader path instead of calling NULL. */
   glsym_private_resolve_es31((void *(*)(const char *))hw_render.get_proc_address);
   log_cb(RETRO_LOG_INFO,
          "ES 3.1 entry points: glBindImageTexture=%p glDispatchCompute=%p glMemoryBarrier=%p",
          (void *)__rglgen_glBindImageTexture, (void *)__rglgen_glDispatchCompute,
          (void *)__rglgen_glMemoryBarrier);
   if (!__rglgen_glBindImageTexture || !__rglgen_glDispatchCompute || !__rglgen_glMemoryBarrier)
   {
      log_cb(RETRO_LOG_WARN,
             "RBG compute shader unavailable (missing ES 3.1 image/compute entry points): using the fragment-shader path");
      g_rbg_use_compute_shader = 0;
      if (VIDCore)
         VIDCore->SetSettingValue(VDP_SETTING_RBG_USE_COMPUTESHADER, g_rbg_use_compute_shader);
   }
#endif
   if (first_ctx_reset == 1)
   {
      first_ctx_reset = 0;
      YabauseInit(&yinit);
      renderer_running = true;
      retro_set_resolution();
      OSDChangeCore(OSDCORE_DUMMY);
      set_memory_maps();   /* HighWram/LowWram exist only after YabauseInit() */
   }
   else
   {
      if (!renderer_running)
         VIDCore->Init();
      renderer_running = true;
      retro_set_resolution();
      set_memory_maps();
   }
#if defined(YAB_CORE_SHARED_CONTEXT)
   /* Engine is up: give the frontend's thread its own context back (unless an
    * outer borrow is still holding it; see outer_borrow above). */
   YK_TRACE("context_reset engine-up now=%p (outer_borrow=%d)",
         (void *)(yk.GetCurrentContext ? yk.GetCurrentContext() : NULL), outer_borrow);
   if (!outer_borrow)
      yk_restore_front();
#endif
}

static void context_destroy(void)
{
#if defined(YAB_CORE_SHARED_CONTEXT)
   /* The frontend calls context_destroy before retro_unload_game, so the
    * VDP thread has not been joined yet: stop it, then hold the shared
    * context on this thread for the engine's GL teardown.  yk_attach()
    * remembers the frontend's context and yk_detach() hands it back, so the
    * frontend keeps drawing with the context it thinks it owns. */
   Vdp2StopRenderThread();
   yk_attach();
#endif
   if (renderer_running)
      VIDCore->DeInit();
   renderer_running = false;
#if defined(YAB_CORE_SHARED_CONTEXT)
   yk_detach();
#endif
#if !defined(_USEGLEW_)
   glsm_ctl(GLSM_CTL_STATE_CONTEXT_DESTROY, NULL);
#endif
}

static bool retro_init_hw_context(void)
{
#if defined(_USEGLEW_)
   hw_render.context_reset = context_reset;
   hw_render.context_destroy = context_destroy;
   hw_render.depth = true;
   hw_render.bottom_left_origin = true;
#ifdef _OGLES3_
   hw_render.context_type = RETRO_HW_CONTEXT_OPENGLES3;
   if (!environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER, &hw_render))
      return false;
#else
   hw_render.context_type = RETRO_HW_CONTEXT_OPENGL;
   if (!environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER, &hw_render))
       return false;
#endif
#else
   glsm_ctx_params_t params = {0};
   params.context_reset = context_reset;
   params.context_destroy = context_destroy;
   params.environ_cb = environ_cb;
   params.stencil = true;
#ifdef _OGLES3_
   params.context_type = RETRO_HW_CONTEXT_OPENGLES_VERSION;
   params.major = 3;
   params.minor = 1;
   if (!glsm_ctl(GLSM_CTL_STATE_CONTEXT_INIT, &params))
      return false;
#else
   params.context_type = RETRO_HW_CONTEXT_OPENGL;
   if (!glsm_ctl(GLSM_CTL_STATE_CONTEXT_INIT, &params))
      return false;
#endif
#endif
   return true;
}

/************************************
 * libretro implementation
 ************************************/

static struct retro_system_av_info g_av_info;

void retro_get_system_info(struct retro_system_info *info)
{
   memset(info, 0, sizeof(*info));
   info->library_name     = "YabaSanshiro";
#ifndef GIT_VERSION
#define GIT_VERSION ""
#endif
   info->library_version  = "v" VERSION GIT_VERSION;
   info->need_fullpath    = true;
   info->block_extract    = false;
   info->valid_extensions = "bin|ccd|chd|cue|iso|mds|m3u|zip";
}

void check_variables(void)
{
   struct retro_variable var;

   var.key = "yabasanshiro_force_hle_bios";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "disabled") == 0 && hle_bios_force)
         hle_bios_force = false;
      else if (strcmp(var.value, "enabled") == 0 && !hle_bios_force)
         hle_bios_force = true;
   }

   var.key = "yabasanshiro_frameskip";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "enabled") == 0)
         g_frame_skip = 1;
      else if (strcmp(var.value, "disabled") == 0)
         g_frame_skip = 0;
   }

   var.key = "yabasanshiro_rbg_resolution_mode";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "original") == 0)
      {
         g_rbg_resolution_mode = 0;
      }
      else if (strcmp(var.value, "2x") == 0)
      {
         g_rbg_resolution_mode = 1;
      }
      else if (strcmp(var.value, "720p") == 0)
      {
         g_rbg_resolution_mode = 2;
      }
      else if (strcmp(var.value, "1080p") == 0)
      {
         g_rbg_resolution_mode = 3;
      }
   }

   var.key = "yabasanshiro_rbg_use_compute_shader";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "enabled") == 0)
         g_rbg_use_compute_shader = 1;
      else if (strcmp(var.value, "disabled") == 0)
         g_rbg_use_compute_shader = 0;
   }

#ifdef DYNAREC_DEVMIYAX
   var.key = "yabasanshiro_sh2coretype";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "dynarec") == 0)
         g_sh2coretype = 3;
      else if (strcmp(var.value, "interpreter") == 0)
         g_sh2coretype = SH2CORE_INTERPRETER;
   }
#endif

   var.key = "yabasanshiro_addon_cart";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "none") == 0 && addon_cart_type != CART_NONE)
         addon_cart_type = CART_NONE;
      else if (strcmp(var.value, "1M_extended_ram") == 0 && addon_cart_type != CART_DRAM8MBIT)
         addon_cart_type = CART_DRAM8MBIT;
      else if (strcmp(var.value, "4M_extended_ram") == 0 && addon_cart_type != CART_DRAM32MBIT)
         addon_cart_type = CART_DRAM32MBIT;
   }

   var.key = "yabasanshiro_multitap_port1";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "disabled") == 0)
         multitap[0] = 0;
      else if (strcmp(var.value, "enabled") == 0)
         multitap[0] = 1;
   }

   var.key = "yabasanshiro_multitap_port2";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "disabled") == 0)
         multitap[1] = 0;
      else if (strcmp(var.value, "enabled") == 0)
         multitap[1] = 1;
   }

   var.key = "yabasanshiro_resolution_mode";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "original") == 0)
      {
         resolution_mode = 1;
         g_resolution_mode = 3;
      }
      else if (strcmp(var.value, "2x") == 0)
      {
         resolution_mode = 2;
         g_resolution_mode = 2;
      }
#ifndef LOW_END
      else if (strcmp(var.value, "4x") == 0)
      {
         resolution_mode = 4;
         g_resolution_mode = 1;
      }
#endif
   }

#ifdef ALLOW_POLYGON_MODE
   var.key = "yabasanshiro_polygon_mode";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "perspective_correction") == 0)
         polygon_mode = PERSPECTIVE_CORRECTION;
      else if (strcmp(var.value, "gpu_tesselation") == 0)
         polygon_mode = GPU_TESSERATION;
      else if (strcmp(var.value, "cpu_tesselation") == 0)
         polygon_mode = CPU_TESSERATION;
   }
#endif
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
   memset(info, 0, sizeof(*info));

   if(initial_resolution_mode == 0)
   {
      // Get the initial resolution mode at start
      // It will be the resolution_mode limit until the core is restarted
      check_variables();
      initial_resolution_mode = resolution_mode;
   }

   info->timing.fps            = (retro_get_region() == RETRO_REGION_NTSC) ? 60.0f : 50.0f;
   info->timing.sample_rate    = SAMPLERATE;
   info->geometry.base_width   = game_width;
   info->geometry.base_height  = game_height;
   // No need to go above 8x what is needed by Hi-Res games, we disallow 16x for Hi-Res games
   info->geometry.max_width    = 704 * (initial_resolution_mode == max_resolution_mode ? max_resolution_mode/2 : initial_resolution_mode);
   info->geometry.max_height   = 512 * (initial_resolution_mode == max_resolution_mode ? max_resolution_mode/2 : initial_resolution_mode);
   info->geometry.aspect_ratio = (retro_get_region() == RETRO_REGION_NTSC) ? 4.0 / 3.0 : 5.0 / 4.0;
}

void retro_set_controller_port_device(unsigned port, unsigned device)
{
   if(pad_type[port] != device)
   {
      pad_type[port] = device;
      if(PERCore)
         PERCore->Init();
   }
}

/* libretro requires retro_serialize_size() to stay constant for the lifetime of
 * a loaded game, but Yabause's raw state size varies a little frame to frame
 * (VDP1 command lists, movie data, ...).  Probe once, then return a generous
 * fixed upper bound for the rest of the session and zero-pad every serialize to
 * fill it.  Reset per content in retro_load_game_common().
 *
 * This is the reference implementation (lr-yabasanshiro
 * src/libretro/libretro.c:976-1034).  It replaces the previous no-op stubs that
 * returned size 0 / true, which left the frontend unable to save states at all
 * (minarch's State_write() bails out when serialize_size() is 0) while making
 * every load look successful.  The old dead code below them memcpy'd the
 * caller's size out of a freshly allocated buffer with no bounds check. */
static size_t g_serialize_size = 0;

size_t retro_serialize_size(void)
{
   void *buffer = NULL;
   size_t size = 0;
   int error;

   if (g_serialize_size != 0)
      return g_serialize_size;

   ScspMuteAudio(SCSP_MUTE_SYSTEM);
   error = YabSaveStateBuffer (&buffer, &size);
   ScspUnMuteAudio(SCSP_MUTE_SYSTEM);

   free(buffer);

   if (error || size == 0)
      return 0; /* not ready yet -- do not cache, the frontend retries */

   /* +25% +2MB headroom over the first observed state size. */
   g_serialize_size = size + (size / 4) + (2 * 1024 * 1024);
   return g_serialize_size;
}

bool retro_serialize(void *data, size_t size)
{
   void *buffer = NULL;
   size_t out_size = 0;
   int error;

   ScspMuteAudio(SCSP_MUTE_SYSTEM);
   error = YabSaveStateBuffer (&buffer, &out_size);
   ScspUnMuteAudio(SCSP_MUTE_SYSTEM);

   if (error || !buffer)
      return false;

   if (out_size > size)
   {
      /* State grew past the cached upper bound -- fail rather than truncate. */
      free(buffer);
      return false;
   }

   memcpy(data, buffer, out_size);
   if (out_size < size)
      memset((unsigned char *)data + out_size, 0, size - out_size);
   free(buffer);
   return true;
}

/* ---------------------------------------------------------------------------
 * State loads apply immediately.
 *
 * This used to be a "warm-up gate": a load arriving during the first ~2 s of
 * boot -- exactly what resume-on-launch does -- was stashed and replayed from
 * retro_run() once the machine was warm, because loading it cold left the sound
 * path silent for the rest of the session.
 *
 * That was a workaround for a real bug in the SCSP savestate, fixed at the
 * source: the sound CPU (68k) register block was delegated to M68K->SaveState()
 * / M68K->LoadState(), which the Musashi interface implemented as empty stubs,
 * so no state file carried any 68k state; and restoring the stack pointers
 * before writing SR made Musashi swap them whenever the live mode differed from
 * the saved one.  With both fixed a state loads correctly at any frame (verified
 * on device: a load at frame 1 now matches a plain boot sample for sample), so
 * the gate is gone.  See .notes/audio-ab/STATE-68K-ROOTCAUSE.md.
 *
 * Caveat: state files written before that fix (SCSP chunk version < 5) contain
 * no 68k state at all and still cannot be replayed correctly from a cold start;
 * re-save them with this build.
 * ------------------------------------------------------------------------- */
static int apply_state_buffer(const void *data, size_t size)
{
   int error = YabLoadStateBuffer(data, size);
#if defined(YAB_CORE_SHARED_CONTEXT)
   /* retro_set_resolution() rebuilds the video core's GL objects, and the thread
    * driving retro_run does not own the core's shared context -- borrow it from
    * the VDP thread for the duration, exactly like the option-change and
    * res_pending paths in retro_run(). */
   VdpRevoke();
   yk_attach();
   retro_set_resolution();
   yk_detach();
   VdpResume();
#else
   retro_set_resolution();
#endif
   return error;
}

bool retro_unserialize(const void *data, size_t size)
{
   return apply_state_buffer(data, size) == 0;
}

void retro_cheat_reset(void)
{
   CheatClearCodes();
}

void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
   (void)index;
   (void)enabled;
   (void)code;

   if (CheatAddARCode(code) == 0)
      return;
}

static int does_file_exist(const char *filename)
{
   struct stat st;
   int result = stat(filename, &st);
   return result == 0;
}

static void extract_basename(char *buf, const char *path, size_t size)
{
   strncpy(buf, path_basename(path), size - 1);
   buf[size - 1] = '\0';

   char *ext = strrchr(buf, '.');
   if (ext)
      *ext = '\0';
}

void retro_init(void)
{
   struct retro_log_callback log;
   const char *dir = NULL;

   log_cb                   = NULL;
   perf_get_cpu_features_cb = NULL;
   uint64_t serialization_quirks = RETRO_SERIALIZATION_QUIRK_SINGLE_SESSION;
   /* Performance level for interpreter CPU core is 16 */
   unsigned level           = 16;

   if (environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log))
      log_cb = log.log;

   if (environ_cb(RETRO_ENVIRONMENT_GET_PERF_INTERFACE, &perf_cb))
      perf_get_cpu_features_cb = perf_cb.get_cpu_features;

   if (environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &dir) && dir)
   {
      strncpy(g_system_dir, dir, sizeof(g_system_dir));
   }

   if (environ_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &dir) && dir)
   {
      strncpy(g_save_dir, dir, sizeof(g_save_dir));
   }

   char save_dir[PATH_MAX];
   snprintf(save_dir, sizeof(save_dir), "%s%cyabasanshiro%c", g_save_dir, slash, slash);
   path_mkdir(save_dir);

   if (environ_cb(RETRO_ENVIRONMENT_GET_INPUT_BITMASKS, NULL))
      libretro_supports_bitmasks = true;

   environ_cb(RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL, &level);

   environ_cb(RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS, &serialization_quirks);
}


/* ---- Disk control (multi-disc swap / .m3u playlists) ----------------------
 * Ported from the reference libretro core (lr-yabasanshiro).  Saturn games like
 * Panzer Dragoon Saga, Shining Force III and Grandia ship on several CDs; the
 * frontend drives the virtual tray through this interface and the media change
 * is delegated to cs2.c (Cs2ForceOpenTray / Cs2ForceCloseTray reopen the ISO CD
 * core on a new image). */

#define YABA_MAX_DISKS 16

static char     disk_paths[YABA_MAX_DISKS][PATH_MAX];
static char     disk_labels[YABA_MAX_DISKS][PATH_MAX];
static unsigned disk_count        = 0;
static unsigned disk_index        = 0;
static unsigned disk_initial_idx  = 0;
static char     disk_initial_path[PATH_MAX];
static bool     disk_tray_open    = false;

static int ext_is_m3u(const char *path)
{
   const char *e = path_get_extension(path);
   return e && (e[0] == 'm' || e[0] == 'M')
            && (e[1] == '3')
            && (e[2] == 'u' || e[2] == 'U')
            &&  e[3] == '\0';
}

static void disk_set_label_from_path(unsigned idx)
{
   char base[PATH_MAX];
   if (idx >= YABA_MAX_DISKS || disk_paths[idx][0] == '\0')
      return;
   strncpy(base, path_basename(disk_paths[idx]), sizeof(base) - 1);
   base[sizeof(base) - 1] = '\0';
   path_remove_extension(base);
   strncpy(disk_labels[idx], base, sizeof(disk_labels[idx]) - 1);
   disk_labels[idx][sizeof(disk_labels[idx]) - 1] = '\0';
}

static bool disk_set_eject_state(bool ejected)
{
   if (ejected)
      Cs2ForceOpenTray();
   else
   {
      if (disk_index >= disk_count || disk_paths[disk_index][0] == '\0')
         return false;
      if (Cs2ForceCloseTray(CDCORE_ISO, disk_paths[disk_index]) != 0)
         return false;
      /* keep yabause's notion of the loaded image in sync */
      snprintf(full_path, sizeof(full_path), "%s", disk_paths[disk_index]);
   }
   disk_tray_open = ejected;
   return true;
}

static bool     disk_get_eject_state(void) { return disk_tray_open; }
static unsigned disk_get_image_index(void) { return disk_index; }
static unsigned disk_get_num_images(void)  { return disk_count; }

static bool disk_set_image_index(unsigned index)
{
   /* The media is only swapped when the tray is closed (set_eject_state(false));
    * here we just record which image will be inserted. index == disk_count is the
    * frontend's "no disk" sentinel. */
   if (index == disk_count)
   {
      disk_index = index;
      return true;
   }
   if (index >= disk_count)
      return false;
   disk_index = index;
   return true;
}

static bool disk_replace_image_index(unsigned index,
      const struct retro_game_info *info)
{
   if (index >= YABA_MAX_DISKS)
      return false;

   if (!info || !info->path) /* remove this image */
   {
      unsigned i;
      if (index >= disk_count)
         return false;
      for (i = index; i + 1 < disk_count; i++)
      {
         snprintf(disk_paths[i], PATH_MAX, "%s", disk_paths[i + 1]);
         disk_set_label_from_path(i);
      }
      if (disk_count > 0)
         disk_count--;
      if (disk_count > 0 && disk_index >= disk_count)
         disk_index = disk_count - 1;
      return true;
   }

   snprintf(disk_paths[index], PATH_MAX, "%s", info->path);
   disk_set_label_from_path(index);
   if (index >= disk_count)
      disk_count = index + 1;
   return true;
}

static bool disk_add_image_index(void)
{
   if (disk_count >= YABA_MAX_DISKS)
      return false;
   disk_paths[disk_count][0]  = '\0';
   disk_labels[disk_count][0] = '\0';
   disk_count++;
   return true;
}

static bool disk_set_initial_image(unsigned index, const char *path)
{
   disk_initial_idx = index;
   if (path)
      snprintf(disk_initial_path, sizeof(disk_initial_path), "%s", path);
   else
      disk_initial_path[0] = '\0';
   return true;
}

static bool disk_get_image_path(unsigned index, char *path, size_t len)
{
   if (index >= disk_count || !path || len == 0 || disk_paths[index][0] == '\0')
      return false;
   strncpy(path, disk_paths[index], len - 1);
   path[len - 1] = '\0';
   return true;
}

static bool disk_get_image_label(unsigned index, char *label, size_t len)
{
   if (index >= disk_count || !label || len == 0 || disk_labels[index][0] == '\0')
      return false;
   strncpy(label, disk_labels[index], len - 1);
   label[len - 1] = '\0';
   return true;
}

static const struct retro_disk_control_ext_callback disk_ext_cb = {
   disk_set_eject_state,
   disk_get_eject_state,
   disk_get_image_index,
   disk_set_image_index,
   disk_get_num_images,
   disk_replace_image_index,
   disk_add_image_index,
   disk_set_initial_image,
   disk_get_image_path,
   disk_get_image_label,
};

static const struct retro_disk_control_callback disk_cb = {
   disk_set_eject_state,
   disk_get_eject_state,
   disk_get_image_index,
   disk_set_image_index,
   disk_get_num_images,
   disk_replace_image_index,
   disk_add_image_index,
};

/* Build the disk list from the content path: either an .m3u playlist (one disc
 * per line, '#' comments, paths absolute or relative to the playlist dir) or a
 * single disc image. */
static void disk_init_from_content(const char *content_path)
{
   disk_count     = 0;
   disk_index     = 0;
   disk_tray_open = false;

   if (ext_is_m3u(content_path))
   {
      char basedir[PATH_MAX];
      char line[PATH_MAX];
      FILE *fp;
      fill_pathname_basedir(basedir, content_path, sizeof(basedir));
      fp = fopen(content_path, "r");
      if (fp)
      {
         while (fgets(line, sizeof(line), fp) && disk_count < YABA_MAX_DISKS)
         {
            char *nl;
            char *p = line;
            while (*p == ' ' || *p == '\t')
               p++;
            if (*p == '#' || *p == '\0' || *p == '\r' || *p == '\n')
               continue;
            nl = strpbrk(p, "\r\n");
            if (nl)
               *nl = '\0';
            if (*p == '\0')
               continue;
            if (path_is_absolute(p))
               snprintf(disk_paths[disk_count], PATH_MAX, "%s", p);
            else
               fill_pathname_join(disk_paths[disk_count], basedir, p, PATH_MAX);
            disk_set_label_from_path(disk_count);
            disk_count++;
         }
         fclose(fp);
      }
   }

   if (disk_count == 0) /* single image, or empty/unreadable playlist */
   {
      snprintf(disk_paths[0], PATH_MAX, "%s", content_path);
      disk_set_label_from_path(0);
      disk_count = 1;
   }

   /* honour a frontend-requested initial image (last-used disc restore) */
   if (disk_initial_idx < disk_count &&
       (disk_initial_path[0] == '\0' ||
        strcmp(disk_initial_path, disk_paths[disk_initial_idx]) == 0))
      disk_index = disk_initial_idx;

   disk_initial_idx     = 0;
   disk_initial_path[0] = '\0';
}

static void disk_register(void)
{
   unsigned version = 0;
   if (environ_cb(RETRO_ENVIRONMENT_GET_DISK_CONTROL_INTERFACE_VERSION, &version)
         && version >= 1)
      environ_cb(RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE,
            (void *)&disk_ext_cb);
   else
      environ_cb(RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE,
            (void *)&disk_cb);
}

bool retro_load_game_common()
{
   enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
   g_serialize_size = 0; /* re-probe the savestate size for this content */
   if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
      return false;
   if (!retro_init_hw_context())
      return false;

   yinit.vidcoretype               = VIDCORE_OGL;
   yinit.percoretype               = PERCORE_LIBRETRO;
   yinit.sh2coretype               = g_sh2coretype;
   yinit.sndcoretype               = SNDCORE_LIBRETRO;
#ifdef HAVE_MUSASHI
   yinit.m68kcoretype              = M68KCORE_MUSASHI;
#else
   yinit.m68kcoretype              = M68KCORE_C68K;
#endif
   yinit.mpegpath                  = NULL;
   yinit.frameskip                 = g_frame_skip;
   yinit.rbg_resolution_mode       = g_rbg_resolution_mode;
   yinit.rbg_use_compute_shader    = g_rbg_use_compute_shader;
   yinit.usethreads                = 0;
   yinit.rotate_screen             = 0;
   yinit.skip_load                 = 0;
   yinit.polygon_generation_mode   = polygon_mode;
   yinit.extend_backup             = 0;
   yinit.buppath                   = bup_path;
   yinit.use_new_scsp              = 1;
   yinit.scsp_sync_count_per_frame = 1;
   yinit.extend_backup             = 1;
   yinit.scsp_main_mode            = 1;
   yinit.videoformattype           = VIDEOFORMATTYPE_NTSC;
   yinit.video_filter_type         = 0;

   return true;
}

bool retro_load_game(const struct retro_game_info *info)
{
   if (!info)
      return false;

   check_variables();

   /* Resolve the disc to boot: a single image, or the first entry of an .m3u
    * playlist (disk control can swap to the other entries later). */
   disk_init_from_content(info->path);
   snprintf(full_path, sizeof(full_path), "%s", disk_paths[disk_index]);
   snprintf(bios_path, sizeof(bios_path), "%s%csaturn_bios.bin", g_system_dir, slash);
   if (does_file_exist(bios_path) != 1)
   {
      log_cb(RETRO_LOG_WARN, "%s NOT FOUND\n", bios_path);
      snprintf(bios_path, sizeof(bios_path), "%s%csega_101.bin", g_system_dir, slash);
      if (does_file_exist(bios_path) != 1)
      {
         log_cb(RETRO_LOG_WARN, "%s NOT FOUND\n", bios_path);
         snprintf(bios_path, sizeof(bios_path), "%s%cmpr-17933.bin", g_system_dir, slash);
         if (does_file_exist(bios_path) != 1)
         {
            log_cb(RETRO_LOG_WARN, "%s NOT FOUND\n", bios_path);
         }
      }
   }

   // Real bios is REQUIRED, even if we support HLE bios
   // HLE bios is deprecated and causing more issues than it solves
   // No "autoselect HLE when bios is missing" ever again !
   if (does_file_exist(bios_path) != 1)
   {
      log_cb(RETRO_LOG_ERROR, "We are missing the bios, ABORTING\n");
      return false;
   }
   if (hle_bios_force)
   {
      log_cb(RETRO_LOG_WARN, "HLE bios is enabled, this is for debugging purpose only, expect lots of issues\n");
   }

   snprintf(bup_path, sizeof(bup_path), "%s%cyabasanshiro%cbackup.bin", g_save_dir, slash, slash);

   /* Prime the SAVE_RAM shadow from the core's backup card; the frontend may
    * then overlay its .srm on top before the first frame. */
   sram_seed_from_backup(bup_path);

   struct retro_input_descriptor desc[] = {
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "B" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,     "C" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,     "X" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "A" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,     "Y" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,     "Z" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,    "L" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,    "R" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Start" },
      { 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X, "Analog X" },
      { 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y, "Analog Y" },
      { 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "Analog X (Right)" },
      { 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "Analog Y (Right)" }, 

      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "B" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,     "C" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,     "X" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "A" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,     "Y" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,     "Z" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,    "L" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,    "R" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Start" },
      { 1, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X, "Analog X" },
      { 1, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y, "Analog Y" },
      { 1, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "Analog X (Right)" },
      { 1, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "Analog Y (Right)" }, 

      { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
      { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },
      { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
      { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
      { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "B" },
      { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,     "C" },
      { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,     "X" },
      { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "A" },
      { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,     "Y" },
      { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,     "Z" },
      { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,    "L" },
      { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,    "R" },
      { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Start" },
      { 2, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X, "Analog X" },
      { 2, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y, "Analog Y" },
      { 2, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "Analog X (Right)" },
      { 2, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "Analog Y (Right)" }, 

      { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
      { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },
      { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
      { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
      { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "B" },
      { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,     "C" },
      { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,     "X" },
      { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "A" },
      { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,     "Y" },
      { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,     "Z" },
      { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,    "L" },
      { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,    "R" },
      { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Start" },
      { 3, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X, "Analog X" },
      { 3, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y, "Analog Y" },
      { 3, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "Analog X (Right)" },
      { 3, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "Analog Y (Right)" }, 

      { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
      { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },
      { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
      { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
      { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "B" },
      { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,     "C" },
      { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,     "X" },
      { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "A" },
      { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,     "Y" },
      { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,     "Z" },
      { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,    "L" },
      { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,    "R" },
      { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Start" },
      { 4, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X, "Analog X" },
      { 4, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y, "Analog Y" },
      { 4, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "Analog X (Right)" },
      { 4, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "Analog Y (Right)" }, 

      { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
      { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },
      { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
      { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
      { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "B" },
      { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,     "C" },
      { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,     "X" },
      { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "A" },
      { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,     "Y" },
      { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,     "Z" },
      { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,    "L" },
      { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,    "R" },
      { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Start" },
      { 5, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X, "Analog X" },
      { 5, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y, "Analog Y" },
      { 5, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "Analog X (Right)" },
      { 5, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "Analog Y (Right)" }, 

      { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
      { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },
      { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
      { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
      { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "B" },
      { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,     "C" },
      { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,     "X" },
      { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "A" },
      { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,     "Y" },
      { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,     "Z" },
      { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,    "L" },
      { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,    "R" },
      { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Start" },
      { 6, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X, "Analog X" },
      { 6, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y, "Analog Y" },
      { 6, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "Analog X (Right)" },
      { 6, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "Analog Y (Right)" }, 

      { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
      { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },
      { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
      { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
      { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "B" },
      { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,     "C" },
      { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,     "X" },
      { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "A" },
      { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,     "Y" },
      { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,     "Z" },
      { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,    "L" },
      { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,    "R" },
      { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Start" },
      { 7, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X, "Analog X" },
      { 7, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y, "Analog Y" },
      { 7, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "Analog X (Right)" },
      { 7, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "Analog Y (Right)" }, 

      { 8, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
      { 8, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },
      { 8, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
      { 8, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
      { 8, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "B" },
      { 8, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,     "C" },
      { 8, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,     "X" },
      { 8, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "A" },
      { 8, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,     "Y" },
      { 8, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,     "Z" },
      { 8, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,    "L" },
      { 8, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,    "R" },
      { 8, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Start" },
      { 8, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X, "Analog X" },
      { 8, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y, "Analog Y" },
      { 8, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "Analog X (Right)" },
      { 8, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "Analog Y (Right)" }, 

      { 9, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
      { 9, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },
      { 9, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
      { 9, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
      { 9, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "B" },
      { 9, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,     "C" },
      { 9, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,     "X" },
      { 9, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "A" },
      { 9, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,     "Y" },
      { 9, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,     "Z" },
      { 9, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,    "L" },
      { 9, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,    "R" },
      { 9, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Start" },
      { 9, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X, "Analog X" },
      { 9, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y, "Analog Y" },
      { 9, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "Analog X (Right)" },
      { 9, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "Analog Y (Right)" }, 

      { 10, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
      { 10, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },
      { 10, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
      { 10, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
      { 10, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "B" },
      { 10, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,     "C" },
      { 10, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,     "X" },
      { 10, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "A" },
      { 10, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,     "Y" },
      { 10, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,     "Z" },
      { 10, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,    "L" },
      { 10, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,    "R" },
      { 10, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Start" },
      { 10, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X, "Analog X" },
      { 10, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y, "Analog Y" },
      { 10, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "Analog X (Right)" },
      { 10, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "Analog Y (Right)" }, 

      { 11, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
      { 11, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },
      { 11, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
      { 11, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
      { 11, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "B" },
      { 11, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,     "C" },
      { 11, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,     "X" },
      { 11, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "A" },
      { 11, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,     "Y" },
      { 11, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,     "Z" },
      { 11, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,    "L" },
      { 11, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,    "R" },
      { 11, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Start" },
      { 11, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X, "Analog X" },
      { 11, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y, "Analog Y" },
      { 11, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "Analog X (Right)" },
      { 11, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "Analog Y (Right)" }, 

      { 0 },
   };

   environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, desc);

   yinit.cdcoretype       = CDCORE_ISO;
   yinit.cdpath           = full_path;
   yinit.biospath         = (hle_bios_force ? NULL : bios_path);
   yinit.carttype         = addon_cart_type;
   yinit.cartpath         = "\0";

   return retro_load_game_common();
}

bool retro_load_game_special(unsigned game_type, const struct retro_game_info *info, size_t num_info)
{
   return false;
}

void retro_unload_game(void)
{
   if (!renderer_running)
      VIDCore->Init();
   YabauseDeInit();
}

unsigned retro_get_region(void)
{
   return RETRO_REGION_NTSC;
}

unsigned retro_api_version(void)
{
    return RETRO_API_VERSION;
}

void *retro_get_memory_data(unsigned id)
{
   switch (id)
   {
      case RETRO_MEMORY_SAVE_RAM:   return sram_shadow;
      case RETRO_MEMORY_SYSTEM_RAM: return HighWram;
      default:                      return NULL;
   }
}

size_t retro_get_memory_size(unsigned id)
{
   switch (id)
   {
      case RETRO_MEMORY_SAVE_RAM:   return sizeof(sram_shadow);
      case RETRO_MEMORY_SYSTEM_RAM: return 0x100000;
      default:                      return 0;
   }
}

void retro_deinit(void)
{
   libretro_supports_bitmasks = false;
}

void retro_reset(void)
{
   YabauseReset();
   VdpResume();
   // The following function crashes the core when you use "restart"
   //YabauseResetButton();
}

void reset_global_gl_state()
{
   glUseProgram(0);
   glGetError();
   glBindBuffer(GL_ARRAY_BUFFER, 0);
   glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
   glDisableVertexAttribArray(0);
   glDisableVertexAttribArray(1);
   glDisableVertexAttribArray(2);
   glDisable(GL_DEPTH_TEST);
   glDisable(GL_SCISSOR_TEST);
   glDisable(GL_STENCIL_TEST);
   glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);   
}

void retro_run(void)
{
   unsigned i;
   bool updated  = false;
   one_frame_rendered = false;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated)
   {
#if defined(YAB_CORE_SHARED_CONTEXT)
      /* Resolution/filter changes touch GL: borrow the shared context from
       * the VDP thread for the duration (standalone pattern:
       * retro_arena/main.cpp VdpRevoke() -> MakeCurrent(main) -> ... ->
       * MakeCurrent(NULL) -> VdpResume()). */
      VdpRevoke();
      yk_attach();
#endif
      int prev_resolution_mode = resolution_mode;
      int prev_multitap[2] = {multitap[0],multitap[1]};
      check_variables();
      if(prev_resolution_mode != resolution_mode)
         retro_set_resolution();
      // Unlike Kronos, this core dislike changing tesselation on the fly
      //VIDCore->SetSettingValue(VDP_SETTING_POLYGON_MODE, polygon_mode);
      VIDCore->SetSettingValue(VDP_SETTING_RBG_RESOLUTION_MODE, g_rbg_resolution_mode);
      VIDCore->SetSettingValue(VDP_SETTING_RBG_USE_COMPUTESHADER, g_rbg_use_compute_shader);
      if(PERCore && (prev_multitap[0] != multitap[0] || prev_multitap[1] != multitap[1]))
         PERCore->Init();
      if(g_frame_skip == 1)
         EnableAutoFrameSkip();
      else
         DisableAutoFrameSkip();
#if defined(YAB_CORE_SHARED_CONTEXT)
      yk_detach();
      VdpResume();
#endif
   }

   //YabauseExec(); runs from handle events
   if(PERCore)
      PERCore->HandleEvents();

   /* Keep the frontend's SAVE_RAM (.srm) and the live backup RAM in sync.  Runs
    * on the same thread that just emulated the frame, so no locking is needed. */
   sram_sync();

#if defined(YAB_CORE_SHARED_CONTEXT)
   /* The async VDP thread only marks frames: the libretro contract wants
    * video_cb (and the frontend's shader chain + swap it triggers) on the
    * thread that drives retro_run. */
   if (yk.res_pending)
   {
      yk.res_pending = 0;
      YK_TRACE("res_pending begin cur_w=%dx%d now=%p", game_width, game_height,
            (void *)(yk.GetCurrentContext ? yk.GetCurrentContext() : NULL));
      VdpRevoke();
      yk_attach();
      YK_TRACE("res_pending engine-GL start now=%p (sub=%p)", 
            (void *)(yk.GetCurrentContext ? yk.GetCurrentContext() : NULL), (void *)yk.ctx_sub);
      retro_set_resolution();
      YK_TRACE("res_pending engine-GL done now=%p", 
            (void *)(yk.GetCurrentContext ? yk.GetCurrentContext() : NULL));
      yk_detach();
      VdpResume();
   }
   if (yk.frame_pending)
   {
      yk.frame_pending = 0;
      video_cb(RETRO_HW_FRAME_BUFFER_VALID, current_width, current_height, 0);
      one_frame_rendered = true;
   }
   if (!one_frame_rendered)
      video_cb(NULL, current_width, current_height, 0);
#else
   // If no frame rendered, dupe
   if(!one_frame_rendered)
      video_cb(NULL, current_width, current_height, 0);

   reset_global_gl_state();
#endif
}

#ifdef ANDROID
#include <wchar.h>

size_t mbstowcs(wchar_t *pwcs, const char *s, size_t n)
{
   if (!pwcs)
      return strlen(s);
   return mbsrtowcs(pwcs, &s, n, NULL);
}

size_t wcstombs(char *s, const wchar_t *pwcs, size_t n)
{
   return wcsrtombs(s, &pwcs, n, NULL);
}

#endif
