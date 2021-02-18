#pragma once
#include "def.h"
#include "stringview.h"
#include "traits.h"

#include <type_traits>
#include <typeindex>

extern "C" { typedef struct lua_State lua_State; }

BEGIN_JOYFLOW_NAMESPACE

struct PrimTypeDefinition
{
  DataType typeEnum = DataType::CUSTOM;
  String   typeName = "";

  bool   (*copy)(void* dst, void const* src) = nullptr; //< copy src to dst
  bool   (*move)(void* dst, void* src)       = nullptr; //< move src to dst, invalidate src
  bool   (*destroy)(void* obj)               = nullptr; //< destroys the object

  String (*toString)(void const* obj, sint lengthLimit) = nullptr; // convert obj to string; lengthLimit<=0 means no limit
  bool   (*fromSting)(void* obj, StringView const& str) = nullptr; // convert string to obj

  int    (*pushLua)(void const* obj, lua_State* L)      = nullptr; //< push to lua stack
  bool   (*fromLua)(void* obj, lua_State* L, int stkId) = nullptr; //< get from lua stack
};


template <typename T>
struct PrimTypeDefinitionHelper
{
  // ref: https://stackoverflow.com/questions/87372/check-if-a-class-has-a-member-function-of-a-given-signature
#define DEFINE_FUNCTION_CHECKER(func) \
  template <typename, typename Ret>\
  struct has_##func { static_assert(std::integral_constant<Ret, false>::value, "template parameter should be function type"); };\
  template <typename Cls, typename Ret, typename... Args>\
  struct has_##func<Cls, Ret(Args...)> {\
  private:\
    template<typename U>\
    static constexpr typename\
    std::is_constructible<Ret, decltype(std::declval<U*>()->func(std::declval<Args>()...))>::type check(U*);\
    template<typename>\
    static constexpr std::false_type check(...);\
    typedef decltype(check<Cls>(nullptr)) type;\
  public:\
    static constexpr bool value = type::value;\
  };

  DEFINE_FUNCTION_CHECKER(toString)
  DEFINE_FUNCTION_CHECKER(fromString)
  DEFINE_FUNCTION_CHECKER(pushLua)
  DEFINE_FUNCTION_CHECKER(fromLua)

  static std::enable_if_t<has_toString<const T, String(sint)>::value, String>
  toString(void const* obj)
  {
    return static_cast<T const*>(obj)->toString();
  }

  static std::enable_if_t<has_fromString<T, bool(StringView)>::value, bool>
  fromString(void* obj, StringView const& str)
  {
    return static_cast<T*>(obj)->fromString(str);
  }

  static std::enable_if_t<has_pushLua<const T, int(lua_State*)>::value, int>
  pushLua(void const* obj, lua_State* L)
  {
    return static_cast<const T*>(obj)->pushLua(L);
  }

  static std::enable_if_t<has_fromLua<T, bool(lua_State*, int)>::value, bool>
  fromLua(void* obj, lua_State* L, int stkId)
  {
    return static_cast<T*>(obj)->fromLua(L, stkId);
  }

  static bool copy(void* dst, void const* src)
  {
    *static_cast<T*>(dst) = *static_cast<T const*>(src);
    return true;
  }

  static bool move(void* dst, void* src)
  {
    *static_cast<T*>(dst) = std::move(*static_cast<T*>(src));
    return true;
  }

  static bool destroy(void* obj)
  {
    static_cast<T*>(obj)->~T();
    return true;
  }

  static constexpr String(*getToStringMethod())(void const*, sint)
  {
    if constexpr(has_toString<const T, String(sint)>::value) {
      return toString;
    } else {
      return nullptr;
    }
  }

  static constexpr bool(*getFromStringMethod())(void*,StringView const&)
  {
    if constexpr(has_fromString<T, bool(StringView)>::value) {
      return fromString;
    } else {
      return nullptr;
    }
  }

  static constexpr int (*getPushLuaMethod())(void const*, lua_State*)
  {
    if constexpr(has_pushLua<const T, int(lua_State*)>::value) {
      return pushLua;
    } else {
      return nullptr;
    }
  }

  static constexpr bool (*getFromLuaMethod())(void*, lua_State*, int)
  {
    if constexpr(has_fromLua<T, bool(lua_State*,int)>::value) {
      return fromLua;
    } else {
      return nullptr;
    }
  }
};

template <typename T>
inline PrimTypeDefinition makePrimTypeDefinition()
{
  using Helper = PrimTypeDefinitionHelper<T>;
  PrimTypeDefinition def = {
    /*datatype*/ DataType::CUSTOM,
    /*typename*/ typeid(T).name(),
    /*copy*/     Helper::copy,
    /*move*/     Helper::move,
    /*destroy*/  Helper::destroy,

    /*toString*/   Helper::getToStringMethod(),
    /*fromString*/ Helper::getFromStringMethod(),

    /*pushLua*/   Helper::getPushLuaMethod(),
    /*fromLua*/   Helper::getFromLuaMethod(),
  };

  return def;
}

class PrimTypeRegistery
{
public:
  CORE_API PrimTypeRegistery& instance();

  virtual DataType                  add(std::type_index type, PrimTypeDefinition const& def) = 0;
  virtual DataType                  getDataType(std::type_index type) const   = 0;
  virtual PrimTypeDefinition const* getDefinition(std::type_index type) const = 0;
  virtual PrimTypeDefinition const* getDefinition(DataType dataType) const    = 0;

  template<typename T>
  DataType add(PrimTypeDefinition const& def)
  {
    return add(std::type_index(typeid(typename std::remove_cv<T>::type)), def);
  }
  template<typename T>
  DataType add()
  {
    return add<T>(makePrimTypeDefinition<T>());
  }
  template<typename T>
  PrimTypeDefinition const* getDefinition() const
  {
    return getDefinition(std::type_index(typeid(typename std::remove_cv<T>::type)));
  }
};


END_JOYFLOW_NAMESPACE

