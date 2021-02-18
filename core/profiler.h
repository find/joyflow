#pragma once

#ifdef PROFILER_ENABLED

#if defined(PROFILER_TRACY)

#include <Tracy.hpp>
#define PROFILER_FRAME(name)        FrameMarkNamed(name)
#define PROFILER_THREAD(name)       /*nothing*/
#define PROFILER_SCOPE(name, color) ZoneScopedNC(name, color)
#define PROFILER_SCOPE_DEFAULT()    ZoneScoped
#define PROFILER_TEXT(text, length) ZoneText(text, length) 

#elif defined(PROFILER_OPTICK) 

#include <optick.h>
#define PROFILER_FRAME(name)        OPTICK_FRAME(name)
#define PROFILER_THREAD(name)       OPTICK_THREAD(name)
#define PROFILER_SCOPE(name, color) OPTICK_EVENT(name, OPTICK_MAKE_CATEGORY(0, (0xff000000 | color)))
#define PROFILER_SCOPE_DEFAULT()    OPTICK_EVENT()
#define PROFILER_TEXT(text, length) OPTICK_TAG("info", text)

#else

#error unknown profiler

#endif

#else

#define PROFILER_FRAME(name)
#define PROFILER_THREAD(name)
#define PROFILER_SCOPE(name, color)
#define PROFILER_SCOPE_DEFAULT()
#define PROFILER_TEXT(text, length)

#endif

