#include "def.h"
#include "opdesc.h"
#include "oplib.h"

#ifdef _WIN32
#include <windows.h>
typedef HMODULE  DllHandle;
#define LOAD_LIB(libpath)      LoadLibraryA((libpath))
#define FREE_LIB(handle)       FreeLibrary(handle)
#define GET_FUNC(handle, name) GetProcAddress(handle, name)
#else
#include <unistd.h>
#include <dlfcn.h>
typedef void* DllHandle;
#define LOAD_LIB(libpath)      dlopen(libpath, RTLD_LAZY)
#define FREE_LIB(handle)       dlclose(handle)
#define GET_FUNC(handle, name) dlsym(handle, name)
#endif

static IMPL_VERSION_INFO()

BEGIN_JOYFLOW_NAMESPACE

static std::map<String, DllHandle>& loadedLibs()
{
  static std::map<String, DllHandle> libs_;
  return libs_;
}

bool openOpLib(String const& dllpath)
{
  auto dll = LOAD_LIB(dllpath.c_str());
  if (!dll) {
    spdlog::warn("failed to load {}", dllpath);
    return false;
  }
  void* funcOpenLib = GET_FUNC(dll, "openLib");
  if (!funcOpenLib) {
    spdlog::warn("lib {} has no `openLib` function", dllpath);
    FREE_LIB(dll);
    return false;
  }
  void* funcCloseLib = GET_FUNC(dll, "closeLib");
  if (!funcCloseLib) {
    spdlog::warn("lib {} has no `closeLib` function", dllpath);
    FREE_LIB(dll);
    return false;
  }
  void* funcVersionInfo = GET_FUNC(dll, "versionInfo");
  if (!funcVersionInfo) {
    spdlog::warn("lib {} has no `versionInfo` function", dllpath);
    FREE_LIB(dll);
    return false;
  }
  OpLibVersionInfo myVersion = versionInfo();
  OpLibVersionInfo dlVersion = reinterpret_cast<OpLibVersionInfo(*)()>(funcVersionInfo)();
  if (myVersion.coreVersion!=dlVersion.coreVersion ||
      HEDLEY_VERSION_DECODE_MAJOR(myVersion.compilerVersion)!=HEDLEY_VERSION_DECODE_MAJOR(dlVersion.compilerVersion) ||
      HEDLEY_VERSION_DECODE_MINOR(myVersion.compilerVersion)!=HEDLEY_VERSION_DECODE_MINOR(dlVersion.compilerVersion) ||
      strcmp(myVersion.compiler, dlVersion.compiler)!=0) {
    spdlog::warn("lib {} has mismatched version", dllpath);
    FREE_LIB(dll);
    return false;
  }
  if (myVersion.buildType != dlVersion.buildType) {
    spdlog::warn("lib {} has mismatched build type", dllpath);
    FREE_LIB(dll);
    return false;
  }
  loadedLibs()[dllpath] = dll;
  reinterpret_cast<void(*)()>(funcOpenLib)();
  spdlog::info("successfully loaded {}", dllpath);
  return true;
}

bool closeOpLib(String const& dllpath)
{
  if (auto itr = loadedLibs().find(dllpath); itr!=loadedLibs().end()) {
    reinterpret_cast<void(*)()>(GET_FUNC(itr->second, "closeLib"))();
    FREE_LIB(itr->second);
    loadedLibs().erase(itr);
    return true;
  } else {
    return false;
  }
}

String defaultOpDir()
{
#ifdef _WIN32
  String pathbuf(MAX_PATH, 0);
  for (; ERROR_INSUFFICIENT_BUFFER == GetModuleFileNameA(0, &pathbuf[0], DWORD(pathbuf.length())); pathbuf.resize(pathbuf.size() * 2))
    ;
  return pathbuf.substr(0, pathbuf.find_last_of('\\'))+"\\op";
#else
  char dest[PATH_MAX];
  memset(dest,0,sizeof(dest)); // readlink does not null terminate!
  if (readlink("/proc/self/exe", dest, PATH_MAX) == -1) {
    spdlog::error("failed to read link");
    return "";
  } else {
    String fullpath = dest;
    return fullpath.substr(0, fullpath.find_last_of('/'))+"/op";
  }
#endif
}

END_JOYFLOW_NAMESPACE

