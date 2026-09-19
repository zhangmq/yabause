#ifndef LIBRETRO_CORE_OPTIONS_H__
#define LIBRETRO_CORE_OPTIONS_H__

/* Core options v2 table, ported from lr-yabasanshiro
 * (src/libretro/libretro_core_options.h): same option set, categories,
 * descriptions and defaults as the reference libretro core.
 *
 * Delta: yabasanshiro_video_core keeps its key but only offers "opengl" --
 * this tree wires the OpenGL video core only (vidsoft.c exists but is not
 * hooked up to the libretro port, and there is no Vulkan core here).
 * Every other key, value order and default is identical to the reference.
 */

#include <stdlib.h>
#include <string.h>

#include <libretro.h>
#include <retro_inline.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Categories */
struct retro_core_option_v2_category option_cats_us[] = {
   {
      "system",
      "System",
      "Configure BIOS, cartridge, video backend and CPU core settings."
   },
   {
      "video",
      "Video",
      "Configure internal resolution, polygon rendering and frame pacing."
   },
   {
      "input",
      "Input",
      "Configure multitap (6Player) adaptors."
   },
   { NULL, NULL, NULL },
};

struct retro_core_option_v2_definition option_defs_us[] = {
{
      "yabasanshiro_video_core",
      "Video Core",
      NULL,
      "Select the rendering backend. Core restart required.",
      NULL,
      "system",
      {
         { "opengl",   "OpenGL" },
         { NULL, NULL },
      },
      "opengl"
   },
{
      "yabasanshiro_addon_cart",
      "Addon Cartridge",
      NULL,
      "Emulate an expansion RAM cartridge required by some games. Core restart required.",
      NULL,
      "system",
      {
         { "4M_extended_ram", "4 MB Extended RAM" },
         { "1M_extended_ram", "1 MB Extended RAM" },
         { NULL, NULL },
      },
      "4M_extended_ram"
   },
{
      "yabasanshiro_sh2coretype",
      "SH2 Core",
      NULL,
      "Dynarec is faster; interpreter is more accurate/stable. Core restart required.",
      NULL,
      "system",
      {
         { "dynarec",     "Dynarec (JIT)" },
         { "interpreter", "Interpreter" },
         { NULL, NULL },
      },
      "dynarec"
   },
{
      "yabasanshiro_force_hle_bios",
      "Force HLE BIOS",
      NULL,
      "Use the built-in high-level BIOS instead of a real BIOS image. Deprecated; for debugging only. Core restart required.",
      NULL,
      "system",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
{
      "yabasanshiro_frameskip",
      "Auto Frameskip",
      NULL,
      "Skip frames to maintain full speed (prevents audio glitches during fast-forward).",
      NULL,
      "video",
      {
         { "enabled",  NULL },
         { "disabled", NULL },
         { NULL, NULL },
      },
      "enabled"
   },
{
      "yabasanshiro_resolution_mode",
      "Resolution Mode",
      NULL,
      "Internal rendering resolution multiplier.",
      NULL,
      "video",
      {
         { "original", "Native" },
         { "2x",       "2x" },
#ifndef LOW_END
         { "4x",       "4x" },
#endif
         { NULL, NULL },
      },
      "original"
   },
{
      "yabasanshiro_rbg_resolution_mode",
      "RBG Resolution Mode",
      NULL,
      "Rendering resolution for rotated (RBG) background planes.",
      NULL,
      "video",
      {
         { "original", "Native" },
         { "2x",       "2x" },
         { "720p",     "720p" },
         { "1080p",    "1080p" },
         { NULL, NULL },
      },
      "original"
   },
{
      "yabasanshiro_rbg_use_compute_shader",
      "RBG Compute Shader",
      NULL,
      "Render rotated (RBG) background planes with a compute shader.",
      NULL,
      "video",
      {
         { "enabled",  NULL },
         { "disabled", NULL },
         { NULL, NULL },
      },
      "enabled"
   },
{
      "yabasanshiro_polygon_mode",
      "Polygon Mode",
      NULL,
      "Method used to render distorted/quad polygons.",
      NULL,
      "video",
      {
         { "perspective_correction", "Perspective Correction" },
         { "gpu_tesselation",        "GPU Tessellation" },
         { "cpu_tesselation",        "CPU Tessellation" },
         { NULL, NULL },
      },
      "perspective_correction"
   },
{
      "yabasanshiro_multitap_port1",
      "6Player Adaptor (Port 1)",
      NULL,
      "Enable a 6-player multitap adaptor on controller port 1.",
      NULL,
      "input",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
{
      "yabasanshiro_multitap_port2",
      "6Player Adaptor (Port 2)",
      NULL,
      "Enable a 6-player multitap adaptor on controller port 2.",
      NULL,
      "input",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   { NULL, NULL, NULL, NULL, NULL, NULL, {{NULL, NULL}}, NULL }
};

static struct retro_core_options_v2 options_us = {
   option_cats_us,
   option_defs_us
};

/* Canonical setter with the reference's fallback chain:
 * v2+intl -> v2 -> v1 core options -> SET_VARIABLES. */
static INLINE void libretro_set_core_options(retro_environment_t environ_cb,
      bool *categories_supported)
{
   unsigned version = 0;

   if (!environ_cb || !categories_supported)
      return;

   *categories_supported = false;

   if (!environ_cb(RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION, &version))
      version = 0;

   if (version >= 2)
   {
      struct retro_core_options_v2_intl core_options_intl;
      core_options_intl.us    = &options_us;
      core_options_intl.local = NULL;

      *categories_supported = environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL,
            &core_options_intl);

      if (!*categories_supported)
         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2, &options_us);
   }
   else
   {
      size_t i, j;
      size_t option_index = 0;
      size_t num_options  = 0;
      struct retro_core_option_definition *option_defs_v1 = NULL;
      struct retro_variable *variables = NULL;
      char **values_buf = NULL;

      while (option_defs_us[num_options].key)
         num_options++;

      option_defs_v1 = (struct retro_core_option_definition *)
            calloc(num_options + 1, sizeof(struct retro_core_option_definition));
      if (!option_defs_v1)
         return;

      for (i = 0; i < num_options; i++)
      {
         option_defs_v1[i].key           = option_defs_us[i].key;
         option_defs_v1[i].desc          = option_defs_us[i].desc;
         option_defs_v1[i].info          = option_defs_us[i].info;
         option_defs_v1[i].default_value = option_defs_us[i].default_value;
         for (j = 0; j < 128; j++)
         {
            option_defs_v1[i].values[j] = option_defs_us[i].values[j];
            if (!option_defs_us[i].values[j].value)
               break;
         }
      }

      if (!environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS, option_defs_v1))
      {
         variables  = (struct retro_variable *)calloc(num_options + 1, sizeof(struct retro_variable));
         values_buf = (char **)calloc(num_options, sizeof(char *));

         if (variables && values_buf)
         {
            for (i = 0; i < num_options; i++)
            {
               size_t buf_len = strlen(option_defs_us[i].desc) + 1;

               for (j = 0; j < 128; j++)
               {
                  const char *value = option_defs_us[i].values[j].value;
                  if (!value) break;
                  buf_len += strlen(value) + 1;
               }

               values_buf[option_index] = (char *)malloc(buf_len + 1);
               if (values_buf[option_index])
               {
                  char *buf = values_buf[option_index];
                  buf[0] = '\0';
                  strcat(buf, option_defs_us[i].desc);
                  strcat(buf, "; ");
                  for (j = 0; j < 128; j++)
                  {
                     const char *value = option_defs_us[i].values[j].value;
                     if (!value) break;
                     strcat(buf, value);
                     if (option_defs_us[i].values[j + 1].value)
                        strcat(buf, "|");
                  }
                  variables[option_index].key   = option_defs_us[i].key;
                  variables[option_index].value = buf;
                  option_index++;
               }
            }
            environ_cb(RETRO_ENVIRONMENT_SET_VARIABLES, variables);
         }
      }

      for (i = 0; i < num_options; i++)
         if (values_buf && values_buf[i]) free(values_buf[i]);
      free(values_buf);
      free(variables);
      free(option_defs_v1);
   }
}

#ifdef __cplusplus
}
#endif

#endif
