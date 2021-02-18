#pragma once
#include "def.h"
#include <fmt/format.h>
#include <cstring>

BEGIN_JOYFLOW_NAMESPACE

#include <string_view>
using StringView = std::string_view;

inline int cmp(StringView const& a, StringView const& b)
{
  int c = std::memcmp(a.data(), b.data(), std::min(a.size(), b.size()));
  if (c)
    return c;
  else
    return a.size() == b.size() ? 0 : a.size() > b.size() ? 1 : -1;
}

END_JOYFLOW_NAMESPACE


