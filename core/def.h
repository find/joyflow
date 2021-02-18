#pragma once

#include <new>
#define BEGIN_JOYFLOW_NAMESPACE namespace joyflow {
#define END_JOYFLOW_NAMESPACE }

#include "intrusiveptr.h"
#include <glm/fwd.hpp>
#include <nlohmann/json_fwd.hpp>
#include <mimalloc.h>
#include <cstdint>
#include <map>
#include <string>
//#include <vector>
#include <exception>
#include <memory>
#include <stdint.h>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#define HAVE_CPP17 (__cplusplus >= 201700L) // C++17

#if defined(_WIN32) && !defined(JFCORE_STATIC)
#ifdef JFCORE_EXPORT
#define CORE_API __declspec(dllexport)
#else
#define CORE_API __declspec(dllimport)
#endif
#else
#define CORE_API
#endif

#define EXPRSTR__(expr) #expr
#define EXPRSTR(expr) EXPRSTR__(expr)
#define CONCATENATE_DIRECT(s1, s2) s1##s2
#define CONCATENATE(s1, s2) CONCATENATE_DIRECT(s1, s2)

#if HAVE_CPP17
#define OVERRIDE_NEW_DELETE                                                                                 \
   void* operator new(size_t sz) { return Allocator::malloc(sz); }                                          \
  void  operator delete(void* ptr) { Allocator::free(ptr); }                                                \
  void* operator new[](size_t sz) { return Allocator::malloc(sz); }                                         \
  void  operator delete[](void* ptr) { Allocator::free(ptr); }                                              \
  void* operator new(size_t sz, std::align_val_t al) { return Allocator::aligned_alloc(sz, size_t(al)); }   \
  void  operator delete(void* ptr, std::align_val_t al) { Allocator::aligned_free(ptr, size_t(al)); }       \
  void* operator new[](size_t sz, std::align_val_t al) { return Allocator::aligned_alloc(sz, size_t(al)); } \
  void  operator delete[](void* ptr, std::align_val_t al) { Allocator::aligned_free(ptr, size_t(al)); }
#else
#define OVERRIDE_NEW_DELETE                                         \
  void* operator new(size_t sz) { return Allocator::malloc(sz); }   \
  void  operator delete(void* ptr) { Allocator::free(ptr); }        \
  void* operator new[](size_t sz) { return Allocator::malloc(sz); } \
  void  operator delete[](void* ptr) { Allocator::free(ptr); }
#endif

BEGIN_JOYFLOW_NAMESPACE

using byte  = uint8_t;
using sint  = ptrdiff_t;
using uint  = uint64_t;
using real  = double;
using vec2  = glm::vec<2, real, glm::defaultp>;
using vec3  = glm::vec<3, real, glm::defaultp>;
using vec4  = glm::vec<4, real, glm::defaultp>;
using mat2  = glm::mat<2, 2, real, glm::defaultp>;
using mat3  = glm::mat<3, 3, real, glm::defaultp>;
using mat4  = glm::mat<4, 4, real, glm::defaultp>;
using ivec2 = glm::ivec2;
using ivec3 = glm::ivec3;
using ivec4 = glm::ivec4;

using Json  = nlohmann::json;

template<class T, size_t alignment = 8>
struct mi_stl_aligned_allocator;

struct MiAllocator
{
  static void* malloc(size_t size)
  {
    auto *ptr = mi_malloc(size);
    if (!ptr)
      throw std::bad_alloc();
    return ptr;
  }
  static void* realloc(void* p, size_t newsize)
  {
    auto *ptr = mi_realloc(p, newsize);
    if (!ptr)
      throw std::bad_alloc();
    return ptr;
  }
  static void  free(void* p) { return mi_free(p); }

  static void* aligned_alloc(size_t size, size_t align) {
    auto *ptr = mi_malloc_aligned(size, align);
    if (!ptr)
      throw std::bad_alloc();
    return ptr;
  }
  static void* aligned_realloc(void* p, size_t newsize, size_t align)
  {
    auto *ptr = mi_realloc_aligned(p, newsize, align);
    if (!ptr)
      throw std::bad_alloc();
    return ptr;
  }
  static void aligned_free(void* p, size_t align) { return mi_free_aligned(p, align); }
};

struct CrtAllocator
{
  static void* malloc(size_t size) { return ::malloc(size); }
  static void* realloc(void* p, size_t newsize) { return ::realloc(p, newsize); }
  static void  free(void* p) { ::free(p); }
#ifdef _MSC_VER
  static void* aligned_alloc(size_t size, size_t align) { return ::_aligned_malloc(size, align); }
  static void* aligned_realloc(void* p, size_t newsize, size_t align) { return ::_aligned_realloc(p, newsize, align); }
  static void  aligned_free(void* p, size_t align) { return ::_aligned_free(p); }
#else
  static void* aligned_alloc(size_t size, size_t align) { return ::aligned_alloc(size, align); }
  static void* aligned_realloc(void* p, size_t newsize, size_t align) { return ::realloc(p, newsize); }
  static void  aligned_free(void* p, size_t align) { return ::free(p); }
#endif
};

#if 0
using Allocator = CrtAllocator;
#else
using Allocator = MiAllocator;
#endif

template<class T>
// using STLAllocator = mi_stl_aligned_allocator<T>;
// using STLAllocator=mi_stl_allocator<T>;
using STLAllocator=std::allocator<T>;

template<typename T, typename U>
using Pair = std::pair<T, U>;
template<typename T,
         typename V,
         typename Hash  = std::hash<T>,
         typename Equal = std::equal_to<T>,
         typename Alloc = STLAllocator<Pair<const T, V>>>
using HashMap = std::unordered_map<T, V, Hash, Equal, Alloc>;
template<typename T,
         typename Hash  = std::hash<T>,
         typename Equal = std::equal_to<T>,
         typename Alloc = STLAllocator<T>>
using HashSet = std::unordered_set<T, Hash, Equal, Alloc>;
using String  = std::string;
// template <typename T, typename Alloc = STLAllocator<T>>
// using Vector  = std::vector<T, Alloc>;
template<typename... T>
using Tuple = std::tuple<T...>;

static const int MAX_TUPLE_SIZE = 16;

// aligned stl allocator {{{
template<class T, size_t alignment>
struct mi_stl_aligned_allocator
{
  typedef T                 value_type;
  typedef std::size_t       size_type;
  typedef std::ptrdiff_t    difference_type;
  typedef value_type&       reference;
  typedef value_type const& const_reference;
  typedef value_type*       pointer;
  typedef value_type const* const_pointer;
  template<class U>
  struct rebind
  {
    typedef mi_stl_aligned_allocator<U> other;
  };

  mi_stl_aligned_allocator() mi_attr_noexcept                                = default;
  mi_stl_aligned_allocator(const mi_stl_aligned_allocator&) mi_attr_noexcept = default;
  template<class U>
  mi_stl_aligned_allocator(const mi_stl_aligned_allocator<U>&) mi_attr_noexcept
  {}
  mi_stl_aligned_allocator select_on_container_copy_construction() const { return *this; }
  void                     deallocate(T* p, size_type) { mi_free_aligned(p, alignment); }

#if (__cplusplus >= 201703L) // C++17
  mi_decl_nodiscard T* allocate(size_type count)
  {
    return static_cast<T*>(mi_malloc_aligned(count * sizeof(T), alignment));
  }
  mi_decl_nodiscard T* allocate(size_type count, const void*) { return allocate(count); }
#else
  mi_decl_nodiscard pointer allocate(size_type count, const void* = 0)
  {
    return static_cast<pointer>(mi_malloc_aligned(count * sizeof(value_type), alignment));
  }
#endif

#if ((__cplusplus >= 201103L) || (_MSC_VER > 1900)) // C++11
  using propagate_on_container_copy_assignment = std::true_type;
  using propagate_on_container_move_assignment = std::true_type;
  using propagate_on_container_swap            = std::true_type;
  using is_always_equal                        = std::true_type;
  template<class U, class... Args>
  void construct(U* p, Args&&... args)
  {
    ::new (p) U(std::forward<Args>(args)...);
  }
  template<class U>
  void destroy(U* p) mi_attr_noexcept
  {
    p->~U();
  }
#else
  void construct(pointer p, value_type const& val) { ::new (p) value_type(val); }
  void destroy(pointer p) { p->~value_type(); }
#endif

  size_type     max_size() const mi_attr_noexcept { return (PTRDIFF_MAX / sizeof(value_type)); }
  pointer       address(reference x) const { return &x; }
  const_pointer address(const_reference x) const { return &x; }
};
template<class T1, class T2>
bool operator==(const mi_stl_aligned_allocator<T1>&,
                const mi_stl_aligned_allocator<T2>&) mi_attr_noexcept
{
  return true;
}
template<class T1, class T2>
bool operator!=(const mi_stl_aligned_allocator<T1>&,
                const mi_stl_aligned_allocator<T2>&) mi_attr_noexcept
{
  return false;
}
// aligned stl allocator }}}

// type safe bitmasks {{{
#define ENABLE_BITWISE_OP_FOR_ENUM_CLASS(Enum) \
inline Enum operator | (Enum lhs, Enum rhs) \
{ \
  using Int = typename std::underlying_type<Enum>::type; \
  return static_cast<Enum>(static_cast<Int>(lhs) | static_cast<Int>(rhs)); \
} \
inline Enum operator & (Enum lhs, Enum rhs) \
{ \
  using Int = typename std::underlying_type<Enum>::type; \
  return static_cast<Enum>(static_cast<Int>(lhs) & static_cast<Int>(rhs)); \
} \
inline Enum operator ^ (Enum lhs, Enum rhs) \
{ \
  using Int = typename std::underlying_type<Enum>::type; \
  return static_cast<Enum>(static_cast<Int>(lhs) ^ static_cast<Int>(rhs)); \
} \
inline Enum operator ~(Enum rhs) \
{ \
  using Int = typename std::underlying_type<Enum>::type; \
  return static_cast<Enum>(~static_cast<Int>(rhs)); \
}\
inline bool operator!(Enum rhs) \
{\
  using Int = typename std::underlying_type<Enum>::type; \
  return 0==static_cast<Int>(rhs); \
}
// }}}

// forward declaration of common types:
class DataCollection;
class DataTable;
class DataColumn;
class OpGraph;
class OpNode;
class OpContext;
class ArgValue;
struct OpDesc;

typedef IntrusivePtr<DataCollection> DataCollectionPtr;
typedef IntrusivePtr<DataTable> DataTablePtr;
typedef IntrusivePtr<DataColumn> DataColumnPtr;

END_JOYFLOW_NAMESPACE
