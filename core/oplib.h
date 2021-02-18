#pragma once
#include "def.h"
#include "oparg.h"
#include "opkernel.h"
#include "opdesc.h"
#include "datatable.h"
#include "opcontext.h"
#include "ophelper.h"
#include "version.h"
#include <hedley.h>

#if defined(DFCORE_STATIC)

#define OPLIB_API static

#else

#if defined(_WIN32)
#define OPLIB_API extern "C" __declspec(dllexport)
#else
#define OPLIB_API extern "C"
#endif

#endif

enum BuildType
{
  BUILDTYPE_UNKNOWN,
  BUILDTYPE_DEBUG,
  BUILDTYPE_SANITIZE,
  BUILDTYPE_PROFILE,
  BUILDTYPE_RELEASE
};

struct OpLibVersionInfo
{
  uint64_t    coreVersion;
  uint64_t    libVersion;
  char const* compiler;
  uint64_t    compilerVersion;
  BuildType   buildType;
};

#ifdef HEDLEY_GNUC_VERSION
#define WRITE_COMPILER_INFO\
  info.compiler = "gcc";\
  info.compilerVersion = HEDLEY_GNUC_VERSION;
#elif defined HEDLEY_CLANG_VERSION
#define WRITE_COMPILER_INFO\
  info.compiler = "clang";\
  info.compilerVersion = HEDLEY_CLANG_VERSION;
#elif defined HEDLEY_MSVC_VERSION
#define WRITE_COMPILER_INFO\
  info.compiler = "msvc";\
  info.compilerVersion = HEDLEY_MSVC_VERSION;
#elif defined HEDLEY_INTEL_VERSION
#define WRITE_COMPILER_INFO\
  info.compiler = "intel";\
  info.compilerVersion = HEDLEY_INTEL_VERSION;
#elif defined HEDLEY_EMSCRIPTEN_VERSION
#define WRITE_COMPILER_INFO\
  info.compiler = "emscripten";\
  info.compilerVersion = HEDLEY_EMSCRIPTEN_VERSION;
#else
#define WRITE_COMPILER_INFO\
  info.compiler = "unknown";\
  info.compilerVersion = 0xfffffffffffffffull;
#endif

#if defined SANITIZE
#define WRITE_BUILD_TYPE\
  info.buildType = BUILDTYPE_SANITIZE;
#elif defined DBEUG
#define WRITE_BUILD_TYPE\
  info.buildType = BUILDTYPE_DEBUG;
#elif defined PROFILE
#define WRITE_BUILD_TYPE\
  info.buildType = BUILDTYPE_PROFILE;
#elif defined NDEBUG
#define WRITE_BUILD_TYPE\
  info.buildType = BUILDTYPE_RELEASE;
#else
#define WRITE_BUILD_TYPE\
  info.buildType = BUILDTYPE_UNKNOWN;
#endif

#define IMPL_VERSION_INFO()      \
OpLibVersionInfo versionInfo()   \
{                                \
  OpLibVersionInfo info;         \
  info.coreVersion = DF_CORE_VERSION; \
  info.libVersion = 0x000001;    \
  WRITE_COMPILER_INFO            \
  WRITE_BUILD_TYPE               \
  return info;                   \
}

#ifdef DFCORE_STATIC
#define RIGISTER_STATIC_OPLIB()    \
static struct OpLibRegister {      \
  OpLibRegister() { openLib(); }   \
  ~OpLibRegister() { /*closeLib();*/ } \
} opLibRegistry_ = {}
#else
#define RIGISTER_STATIC_OPLIB() /*nothing to do*/
#endif

#define DECL_OPLIB()                      \
OPLIB_API OpLibVersionInfo versionInfo(); \
OPLIB_API void             openLib();     \
OPLIB_API void             closeLib();    \

#define IMPL_OPLIB()          \
OPLIB_API IMPL_VERSION_INFO() \
RIGISTER_STATIC_OPLIB()

BEGIN_JOYFLOW_NAMESPACE
CORE_API bool   openOpLib(String const& dllpath);
CORE_API bool   closeOpLib(String const& dllpath);
CORE_API String defaultOpDir();
END_JOYFLOW_NAMESPACE

