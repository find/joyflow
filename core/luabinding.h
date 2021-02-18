#pragma once
#include "def.h"

typedef struct lua_State lua_State;

BEGIN_JOYFLOW_NAMESPACE

CORE_API void bindLuaTypes(lua_State* L, bool readonly = false);

END_JOYFLOW_NAMESPACE

