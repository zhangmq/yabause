#include "glsym/glsym.h"

#if defined(_OGLES3_)
#define RGLGEN_RESOLVE(type, var, name) \
   do { if (!(var)) (var) = (type)get_proc(name); } while (0)

void glsym_private_resolve_es31(void *(*get_proc)(const char *))
{
   if (!get_proc)
      return;
   RGLGEN_RESOLVE(RGLSYMGLMEMORYBARRIERPROC,      __rglgen_glMemoryBarrier,      "glMemoryBarrier");
   RGLGEN_RESOLVE(RGLSYMGLBINDIMAGETEXTUREPROC,   __rglgen_glBindImageTexture,   "glBindImageTexture");
   RGLGEN_RESOLVE(RGLSYMGLDISPATCHCOMPUTEPROC,    __rglgen_glDispatchCompute,    "glDispatchCompute");
   RGLGEN_RESOLVE(RGLSYMGLBINDFRAGDATALOCATIONPROC, __rglgen_glBindFragDataLocation, "glBindFragDataLocation");
   RGLGEN_RESOLVE(RGLSYMGLPATCHPARAMETERIPROC,    __rglgen_glPatchParameteri,    "glPatchParameteri");
}

RGLSYMGLMEMORYBARRIERPROC __rglgen_glMemoryBarrier;
RGLSYMGLBINDFRAGDATALOCATIONPROC __rglgen_glBindFragDataLocation;
RGLSYMGLPATCHPARAMETERIPROC __rglgen_glPatchParameteri;
RGLSYMGLDISPATCHCOMPUTEPROC __rglgen_glDispatchCompute;
RGLSYMGLBINDIMAGETEXTUREPROC __rglgen_glBindImageTexture;
#else
RGLSYMGLTEXTUREBARRIERNVPROC __rglgen_glTextureBarrierNV;
#endif
