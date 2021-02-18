#pragma once

#include "def.h"
#include "log.h"
#include <fmt/format.h>
#include <spdlog/spdlog.h>

BEGIN_JOYFLOW_NAMESPACE

#define DEFINE_EXCEPTION(cls)                                   \
class cls : public std::exception                               \
{                                                               \
  String message_;                                              \
public:                                                         \
  cls(String message) noexcept: message_(std::move(message)) {} \
  char const* what() const noexcept override                    \
  {                                                             \
    return message_.c_str();                                    \
  }                                                             \
}

DEFINE_EXCEPTION(Unimplemented);
DEFINE_EXCEPTION(TypeError);
DEFINE_EXCEPTION(CheckFailure);
DEFINE_EXCEPTION(AssertionFailure);
DEFINE_EXCEPTION(ExecutionError);

#define RUNTIME_CHECK(expr, ...)            \
  do { if(!(expr)) {                       \
    String msg = fmt::format(__VA_ARGS__); \
    spdlog::error(msg);                    \
    throw CheckFailure(msg);               \
  } } while(0)

#define WARN_IF_NOT(expr, ...) \
  do { if(!(expr)) {           \
    spdlog::warn(__VA_ARGS__); \
  } } while(0)

#define ALWAYS_ASSERT(expr)                  \
  do { if (!(expr)) {                        \
    char const* msg = "Assertion failed: "   \
      "\"" EXPRSTR(expr) "\""                \
      " at " __FILE__ ":" EXPRSTR(__LINE__); \
    spdlog::error(msg);                      \
    throw AssertionFailure(msg);             \
  } } while (0)

#define UNIMPLEMENTED(msg) \
  throw Unimplemented(msg)

#ifdef DEBUG
#define DEBUG_ASSERT ALWAYS_ASSERT
#else
#define DEBUG_ASSERT(...) (void)(__VA_ARGS__)
#endif

#define ASSERT DEBUG_ASSERT

END_JOYFLOW_NAMESPACE


