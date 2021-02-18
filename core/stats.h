#pragma once

#include "def.h"
#include "stringview.h"
#include <cstdio>
#include <type_traits>
#include <typeindex>
#include <memory>

BEGIN_JOYFLOW_NAMESPACE

struct ObjectInspector
{
  String (*name)(void const* obj)                = 0;
  size_t (*sizeInBytes)(void const* obj)         = 0;
  size_t (*sizeInBytesShared)(void const* obj)   = 0;
  size_t (*sizeInBytesUnshared)(void const* obj) = 0;
};

class CORE_API Stats
{
public:
  static void   add(std::type_index typeIndex,
                    void const*     address,
                    size_t          size);
  static void   remove(std::type_index typeIndex, void const* address);
  static void   dumpLiving(FILE* file);
  static void   dumpLiving(void(*dumpf)(char const* msg, void* arg), void* arg=nullptr);
  static size_t totalAllocCount();
  static size_t totalAllocCount(std::type_index typeIndex);
  template <class T>
  static size_t totalAllocCount() { return totalAllocCount(std::type_index(typeid(typename std::remove_cv<T>::type))); }
  static size_t livingCount();
  static size_t livingCount(std::type_index typeIndex);
  template <class T>
  static size_t livingCount() { return livingCount(std::type_index(typeid(typename std::remove_cv<T>::type))); }

  static void setInspector(std::type_index typeIndex, ObjectInspector const& inspector);
  template <class T>
  static void setInspector(ObjectInspector const& inspector)
  {
    setInspector(std::type_index(typeid(typename std::remove_cv<T>::type)), inspector);
  }
};

template<class T>
class ObjectTracker
{
public:
  ObjectTracker()
  {
    Stats::add(typeid(typename std::remove_cv<T>::type), static_cast<T*>(this), sizeof(T));
  }
  ObjectTracker(ObjectTracker const& oc)
  {
    Stats::add(typeid(typename std::remove_cv<T>::type), static_cast<T*>(this), sizeof(T));
  }
  ObjectTracker(ObjectTracker&& oc)
  { }

  ~ObjectTracker() { Stats::remove(typeid(typename std::remove_cv<T>::type), static_cast<T const*>(this)); }
};

END_JOYFLOW_NAMESPACE
