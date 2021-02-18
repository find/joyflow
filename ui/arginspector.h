#pragma once
#include <core/oparg.h>
#include <core/opcontext.h>
#include <string_view>

using ArgInspector = bool(*)(std::string_view name, joyflow::OpContext* context, joyflow::ArgValue& arg);

void         setArgInspector(joyflow::ArgType type, ArgInspector inspector);
ArgInspector getArgInspector(joyflow::ArgType type);

