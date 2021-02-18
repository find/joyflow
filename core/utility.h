#pragma once

#include "error.h"
#include "def.h"

#include <algorithm>
#include <cctype>
#include <limits>

BEGIN_JOYFLOW_NAMESPACE

inline real toReal(String const& s)
{
  if (std::is_same<real, float>::value)
    return std::strtof(s.c_str(), nullptr);
  else
    return std::strtod(s.c_str(), nullptr);
}

inline sint toInt(String const& s)
{
  if (std::is_same<sint, int32_t>::value)
    return std::strtol(s.c_str(), nullptr, 10);
  else
    return std::strtoll(s.c_str(), nullptr, 10);
}

// from https://stackoverflow.com/questions/1903954/is-there-a-standard-sign-function-signum-sgn-in-c-c {{{
template <typename T> inline constexpr
int signum(T x, std::false_type)
{
  return T(0) < x;
}

template <typename T> inline constexpr
int signum(T x, std::true_type)
{
  return (T(0) < x) - (x < T(0));
}

template <typename T> inline constexpr
int signum(T x)
{
  return signum(x, std::is_signed<T>());
}
// }}}

template<class Vec>
inline void ensureVectorSize(Vec& v, size_t size)
{
  if (v.size() < size)
    v.resize(size);
}

template<class Vec, class T>
inline void ensureVectorSize(Vec& v, size_t size, T const& fillValue)
{
  if (v.size() < size) {
    size_t const osize = v.size();
    v.resize(size);
    std::fill(v.begin() + osize, v.end(), fillValue);
  }
}

template <class Val, class Map, class Key>
inline Val lookup(Map const& m, Key const& k, Val const& fallback)
{
  auto itr = m.find(k);
  if (itr!=m.end())
    return itr->second;
  return fallback;
}

template<class OrderVec, class Vec>
inline void argsort(OrderVec& order, Vec const& values)
{
  order.resize(values.size());
  for (size_t i = 0, n = values.size(); i < n; ++i) {
    order[i] = i;
  }
  std::sort(
      order.begin(), order.end(), [&values](size_t a, size_t b) { return values[a] < values[b]; });
}

/// increase numeric suffix, if no numeric suffix exists, append one
inline String increaseNumericSuffix(String const& name)
{
  size_t numstart = name.size();
  for (; numstart >= 1 && std::isdigit(name[numstart - 1]); --numstart)
    ;
  String prefix = name.substr(0, numstart);
  String suffix;
  if (numstart >= 0 && numstart < name.size()) {
    int carry = 1;
    suffix    = name.substr(numstart);
    DEBUG_ASSERT(suffix.size() < std::numeric_limits<int>::max());
    // simple big int increment
    // avoid suffix number being too large
    for (int i = static_cast<int>(suffix.size()) - 1; i >= 0; --i) {
      suffix[i] += carry;
      if (suffix[i] > '9') {
        suffix[i] -= 10;
        carry = 1;
      } else {
        carry = 0;
        break;
      }
    }
    if (carry) {
      suffix = "1" + suffix;
    }
  } else {
    suffix = "1";
  }
  return prefix + suffix;
}

template <class Func>
struct DeferExecutor
{
  Func deferCallback;
  DeferExecutor(Func&& f) :deferCallback(f) {}
  ~DeferExecutor() { deferCallback(); }
};
#define DEFER(func) auto CONCATENATE(_deferred_executor_,__LINE__) = DeferExecutor{func}

CORE_API size_t xxhash(void const* data, size_t size);

END_JOYFLOW_NAMESPACE
