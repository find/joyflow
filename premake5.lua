local addprefix = function(prefix, list)
  local result = {}
  for i,v in ipairs(list) do
    table.insert(result, prefix..v)
  end
  return result
end

newoption({
  trigger = 'profiler',
  value   = 'profiler',
  default = 'tracy',
  description = 'Choose a profiler',
  allowed = {
    {'optick', 'Optick / Brofiler'},
    {'tracy',  'Tracy'}
  }
})

newoption({
  trigger = 'static',
  value   = false,
  default = false,
  description = 'Link staticly'
})

workspace('joyflow')
  if _OPTIONS["cc"]=="clang" then
    configurations({'release', 'debug', 'profile', 'sanitize'})
  else
    configurations({'release', 'debug', 'profile'})
  end
  platforms({'native','arm','x86','x64'})
  if _OPTIONS['static'] then
    location('build/static')
    objdir('build/static/%{cfg.buildcfg}_%{cfg.platform}/obj')
    targetdir('build/static/%{cfg.buildcfg}_%{cfg.platform}/bin')
  else
    location('build')
    objdir('build/%{cfg.buildcfg}_%{cfg.platform}/obj')
    targetdir('build/%{cfg.buildcfg}_%{cfg.platform}/bin')
  end
  startproject('tests')

  language('C++')
  defines({'SPDLOG_COMPILED_LIB', 'SPDLOG_FMT_EXTERNAL', 'SOL_ALL_SAFETIES_ON=1'})
  if _OPTIONS['static'] then
    defines({'JFCORE_STATIC'})
  else
    defines({'FMT_SHARED', 'MI_SHARED_LIB', 'TRACY_IMPORTS'})
  end
  includedirs({
    '.',
    'deps/fmt/include',
    'deps/glm',
    'deps/json',
    'deps/mimalloc/include',
    'deps/spdlog/include',
    'deps/tracy',
    'deps/optick/src',
  })
  filter('debug')
    symbols('on')
    optimize('off')
    defines({'DEBUG', '_DEBUG'})
    defines({'USE_OPTICK=0'})
    -- defines({'_GLIBCXX_DEBUG'}) -- sol2 fails to compile with this define :(
  filter({'debug', 'toolset:clang'})
    buildoptions({'-fstandalone-debug'})
  filter('sanitize')
    symbols('on')
    defines({'DEBUG','SANITIZE'})
    defines({'USE_OPTICK=0'})
  filter({'profile'})
    defines({'PROFILER_ENABLED', 'OPTICK_ENABLE_GPU=0'})
    if _OPTIONS['profiler']=='tracy' then
      defines({'PROFILER_TRACY', 'TRACY_ENABLE', 'TRACY_ON_DEMAND'})
      defines({'USE_OPTICK=0'})
    else
      defines({'PROFILER_OPTICK'})
      defines({'USE_OPTICK=1'})
    end
    defines({'NDEBUG','PROFILE'})
    symbols('on')
    optimize('on')
  filter({'release'})
    optimize('on')
    defines({'USE_OPTICK=0'})
    defines({'NDEBUG'})
  if not _OPTIONS['static'] then
    filter('system:windows')
      defines({'LUA_BUILD_AS_DLL'})
  end

project('joyflow')
  kind(_OPTIONS['static'] and 'StaticLib' or 'SharedLib')
  cppdialect('C++17')
  files({'core/**'})
  includedirs({
    'core',
    'core/detail',
    'deps/lua',
    'deps/marl/include',
    'deps/parallel-hashmap/parallel_hashmap',
    'deps/sol2/single/include',
    'deps/xxHash',
    'deps/hedley',
    'deps/pdqsort',
    'deps/fast_float/include',
  })
  if not _OPTIONS['static'] then
    defines({'JFCORE_EXPORT'})
  end
  links({'xxHash', 'mimalloc', 'lua', 'fmt', 'spdlog', 'marl'})
  filter({'action:vs*', 'files:core/luabinding.cpp'})
    buildoptions({'/bigobj'})
  filter('system:not windows')
    links({'pthread'})
  filter({'action:vs*'})
    libdirs('deps/mimalloc/bin')
  filter({'system:windows','platforms:x86'})
    links({'mimalloc-redirect32'})
  filter({'system:windows','platforms:x64'})
    links({'mimalloc-redirect'})
  filter({'toolset:gcc or clang'})
    buildoptions({'-fPIC'})
  filter({'sanitize'})
    buildoptions({'-fsanitize=address'})
    linkoptions({'-fsanitize=address'})
  filter({'profile'})
    if _OPTIONS['profiler']=='tracy' then
      links('tracy')
    else
      links('optick')
    end

local projectnfd = function(root_dir)
  project "nfd"
    kind "StaticLib"

    -- common files
    files {root_dir.."src/*.h",
           root_dir.."src/include/*.h",
           root_dir.."src/nfd_common.c",
    }

    includedirs {root_dir.."src/include/"}

    warnings "extra"

    -- system build filters
    filter "system:windows"
      language "C++"
      files {root_dir.."src/nfd_win.cpp"}

    filter {"action:gmake or action:xcode4"}
      buildoptions {"-fno-exceptions"}

    filter "system:macosx"
      language "C"
      files {root_dir.."src/nfd_cocoa.m"}

    filter {"system:linux"}
      language "C"
      files {root_dir.."src/nfd_zenity.c"}

    -- visual studio filters
    filter "action:vs*"
      defines { "_CRT_SECURE_NO_WARNINGS" }      
end

projectnfd('deps/nativefiledialog/')

project('marl')
  kind('StaticLib')
  includedirs({'deps/marl/include'})
  files({
    'deps/marl/include/**',
    'deps/marl/src/debug.cpp',
    'deps/marl/src/memory.cpp',
    'deps/marl/src/scheduler.cpp',
    'deps/marl/src/thread.cpp',
    'deps/marl/src/trace.cpp',
    'deps/marl/src/osfiber_aarch64.c',
    'deps/marl/src/osfiber_arm.c',
    'deps/marl/src/osfiber_asm_aarch64.S',
    'deps/marl/src/osfiber_asm_arm.S',
    'deps/marl/src/osfiber_asm_mips64.S',
    'deps/marl/src/osfiber_asm_ppc64.S',
    'deps/marl/src/osfiber_asm_x64.S',
    'deps/marl/src/osfiber_asm_x86.S',
    'deps/marl/src/osfiber_mips64.c',
    'deps/marl/src/osfiber_ppc64.c',
    'deps/marl/src/osfiber_x64.c',
    'deps/marl/src/osfiber_x86.c'
  })
  filter({'toolset:gcc or clang'})
    buildoptions({'-fPIC'})

project("ui")
  kind("WindowedApp")
  cppdialect('C++17')
  debugdir('.')
  files({"ui/**"})
  files({
    "deps/nodegraphed/nodegraph.*",
    "deps/nodegraphed/roboto_medium.cpp",
    "deps/nodegraphed/sourcecodepro.cpp",
    "deps/ImGuiColorTextEdit/TextEditor.*",
  })
  includedirs({
    'core',
    'deps/lua',
    'deps/imgui',
    'deps/imgui/backends',
    'deps/nodegraphed',
    'deps/nativefiledialog/src/include',
    'deps/glfw/include',
    'deps/sol2/single/include',
    'deps/hedley',
    'deps/ImGuiColorTextEdit',
  })
  links({'joyflow', 'mimalloc', 'lua', 'fmt', 'spdlog', 'glfw', 'imgui', 'nfd'})
  if _OPTIONS['static'] then
    links({'oplib'})
  end
  filter({'profile'})
    if _OPTIONS['profiler']=='tracy' then
      links('tracy')
    else
      links('optick')
    end
  filter({'system:not windows'})
    links({'pthread', 'dl', 'GL'})
  filter({'system:windows'})
    links({'gdi32', 'opengl32', 'imm32'})
  filter({'sanitize'})
    buildoptions({'-fsanitize=address'})
    linkoptions({'-fsanitize=address'})

project('glfw')
  kind('StaticLib')
  files({'deps/glfw/include/**'})
  files(addprefix('deps/glfw/src/',
    {'internal.h', 'mappings.h', 'context.c', 'init.c', 'input.c', 'monitor.c', 'vulkan.c', 'window.c'}))
  filter({'system:windows'})
    defines({'_GLFW_WIN32=1','_CRT_SECURE_NO_WARNINGS=1'})
    links({'gdi32'})
    files(addprefix('deps/glfw/src/',
          {"win32_platform.h", "win32_joystick.h", "wgl_context.h",
            "egl_context.h", "osmesa_context.h", "win32_init.c",
            "win32_joystick.c", "win32_monitor.c", "win32_time.c",
            "win32_thread.c", "win32_window.c", "wgl_context.c",
            "egl_context.c", "osmesa_context.c"}))
  filter({'system:macosx'})
    defines({'_GLFW_COCOA=1'})
    links({'Cocoa','IOKit','CoreFoundation'})
    files(addprefix('deps/glfw/src/',
           {"cocoa_platform.h", "cocoa_joystick.h", "posix_thread.h",
            "nsgl_context.h", "egl_context.h", "osmesa_context.h",
            "cocoa_init.m", "cocoa_joystick.m", "cocoa_monitor.m",
            "cocoa_window.m", "cocoa_time.c", "posix_thread.c",
            "nsgl_context.m", "egl_context.c", "osmesa_context.c"}))
  filter({'system:linux or bsd'})
    defines({'_GLFW_X11=1'})
    files(addprefix('deps/glfw/src/',
           {"x11_platform.h", "xkb_unicode.h", "posix_time.h",
            "posix_thread.h", "glx_context.h", "egl_context.h",
            "osmesa_context.h", "x11_init.c", "x11_monitor.c",
            "x11_window.c", "xkb_unicode.c", "posix_time.c",
            "posix_thread.c", "glx_context.c", "egl_context.c",
            "osmesa_context.c"}))
  filter({'system:linux'})
    files({'deps/glfw/src/linux_joystick.*'})

project('imgui')
  kind('StaticLib')
  includedirs({'deps/glfw/include','deps/imgui'})
  files({'deps/imgui/*.h',
         'deps/imgui/*.cpp',
         'deps/imgui/misc/cpp/*.h',
         'deps/imgui/misc/cpp/*.cpp',
         'deps/imgui/backends/imgui_impl_glfw.*',
         'deps/imgui/backends/imgui_impl_opengl2.*'})
  --excludes({'deps/imgui/imgui_demo.cpp'})
  links('glfw')

project('tests')
  kind('ConsoleApp')
  cppdialect('C++17')
  debugdir('.')
  includedirs({
    'deps/doctest',
    'deps/lua',
    'deps/sol2/single/include',
    'deps/hedley',
  })
  defines({'DOCTEST_SINGLE_HEADER'})
  files({'tests/**'})
  links({
      'joyflow',
      'mimalloc',
      'lua',
      'fmt',
      'spdlog'
  })
  filter({'system:not windows'})
    links({'pthread', 'dl'})
  filter('sanitize')
    linkoptions({'-fsanitize=address'})
  filter('profile')
    if _OPTIONS['profiler']=='tracy' then
      links('tracy')
    else
      links('optick')
    end

project('xxHash')
  kind('StaticLib')
  files({'deps/xxHash/xxhash.c', 'deps/xxHash/xxhash.h'})
  filter('toolset:gcc or clang')
    buildoptions({'-fPIC'})

project('tracy')
  if _OPTIONS['static'] then
    kind('StaticLib')
  else
    kind('SharedLib')
    defines({'TRACY_ENABLE', 'TRACY_EXPORTS'})
  end
  files({'deps/tracy/*.hpp', 'deps/tracy/TracyClient.cpp'})

project('optick')
  if _OPTIONS['static'] then
    kind('StaticLib')
  else
    kind('SharedLib')
    defines({'OPTICK_EXPORTS'})
  end
  files({'deps/optick/src/*'})

project('fmt')
  if _OPTIONS['static'] then
    kind('StaticLib')
  else
    kind('SharedLib')
    defines({'FMT_EXPORT'})
  end
  files({'deps/fmt/src/*'})

project('mimalloc')
  if _OPTIONS['static'] then
    kind('StaticLib')
  else
    kind('SharedLib')
    defines({'MI_SHARED_LIB_EXPORT'})
  end
  defines({'MI_SECURE=2', 'MI_DEBUG=0'})
  filter({'action:vs*'})
    libdirs('deps/mimalloc/bin')
  filter({'system:windows','platforms:x86'})
    links({'mimalloc-redirect32'})
    postbuildcommands({
      '{COPY} %{path.getabsolute("deps/mimalloc/bin").."/*redirect32.dll"} %{cfg.buildtarget.directory}'
    })
  filter({'system:windows','platforms:x64'})
    links({'mimalloc-redirect'})
    postbuildcommands({
      '{COPY} %{path.getabsolute("deps/mimalloc/bin").."/*redirect.dll"} %{cfg.buildtarget.directory}'
    })
  filter({})
  files({
    'deps/mimalloc/include/*.h',
    'deps/mimalloc/src/stats.c',
    'deps/mimalloc/src/random.c',
    'deps/mimalloc/src/os.c',
    'deps/mimalloc/src/arena.c',
    'deps/mimalloc/src/region.c',
    'deps/mimalloc/src/segment.c',
    'deps/mimalloc/src/page.c',
    'deps/mimalloc/src/alloc.c',
    'deps/mimalloc/src/alloc-aligned.c',
    'deps/mimalloc/src/alloc-posix.c',
    'deps/mimalloc/src/heap.c',
    'deps/mimalloc/src/options.c',
    'deps/mimalloc/src/init.c',
  })

project('lua')
  if _OPTIONS['static'] then
    kind('StaticLib')
  else
    kind('SharedLib')
  end
  files({
    'deps/lua/*.c',
    'deps/lua/*.h'
  })
  excludes({
    'deps/lua/lua.c',
    'deps/lua/luac.c',
    'deps/lua/onelua.c'
  })
  filter('toolset:gcc or clang')
    buildoptions({'-fPIC'})

project('spdlog')
  kind('StaticLib')
  files({
    'deps/spdlog/include/**',
    'deps/spdlog/src/**'
  })
  filter('toolset:gcc or clang')
    buildoptions({'-fPIC'})

project('oplib')
  kind(_OPTIONS['static'] and 'StaticLib' or 'SharedLib')
  cppdialect('C++17')
  targetdir('build/%{cfg.buildcfg}_%{cfg.platform}/bin/op')
  files({'oplib/**'})
  includedirs({
    'core',
    'deps/csv-parser/single_include',
    'deps/hedley',
  })
  links({'joyflow','fmt','mimalloc','spdlog'})

project('natvis')
  kind('Utility')
  files({
    'deps/glm/util/glm.natvis',
    'deps/imgui/misc/natvis/imgui.natvis',
    'deps/json/nlohmann_json.natvis',
    'deps/parallel-hashmap/phmap.natvis',
    'deps/sol2/sol2.natvis',
   })
