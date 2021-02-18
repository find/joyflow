#pragma once

#include "def.h"
#include <type_traits>

BEGIN_JOYFLOW_NAMESPACE

enum class DataType : int16_t
{
  UNKNOWN=-1,
  INT32=0,
  UINT32,
  INT64,
  UINT64,
  FLOAT,
  DOUBLE,
  STRUCTURE,
  STRING,
  BLOB,

  COUNT,
  CUSTOM, // custom types and runtime queries are handled in primtypes.h
};

// TypeInfo {{{
template<class T>
struct TypeInfo
{
  typedef T                 StorageType;
  static constexpr DataType dataType  = DataType::STRUCTURE;
  static constexpr size_t   size      = sizeof(T);
  static constexpr int      tupleSize = 1;
  static constexpr bool     isNumeric = false;
  static constexpr bool     isArray   = false;
  static constexpr bool     isTrivial = std::is_trivial<T>::value;
  static constexpr char const* const name = "unknown"; // TODO
  static StorageType*       address(T& v) { return &v; }
  static StorageType const* address(T const& v) { return &v; }
};
template<>
struct TypeInfo<void>
{
  typedef void              StorageType;
  static constexpr DataType dataType  = DataType::STRUCTURE;
  static constexpr size_t   size      = 0;
  static constexpr int      tupleSize = 0;
  static constexpr bool     isNumeric = false;
  static constexpr bool     isArray   = false;
  static constexpr bool     isTrivial = false;
  static constexpr char const* const name = "void";
};
template<>
struct TypeInfo<int32_t>
{
  typedef int32_t           StorageType;
  static constexpr DataType dataType  = DataType::INT32;
  static constexpr size_t   size      = sizeof(int32_t);
  static constexpr int      tupleSize = 1;
  static constexpr bool     isNumeric = true;
  static constexpr bool     isArray   = false;
  static constexpr bool     isTrivial = true;
  static constexpr char const* const name = "int32_t";
  static StorageType*       address(int32_t& v) { return &v; }
  static StorageType const* address(int32_t const& v) { return &v; }
};
template<>
struct TypeInfo<uint32_t>
{
  typedef int32_t           StorageType;
  static constexpr DataType dataType  = DataType::UINT32;
  static constexpr size_t   size      = sizeof(uint32_t);
  static constexpr int      tupleSize = 1;
  static constexpr bool     isNumeric = true;
  static constexpr bool     isArray   = false;
  static constexpr bool     isTrivial = true;
  static constexpr char const* const name = "uint32_t";
  static StorageType*       address(uint32_t& v) { return reinterpret_cast<StorageType*>(&v); }
  static StorageType const* address(uint32_t const& v)
  {
    return reinterpret_cast<StorageType const*>(&v);
  }
};
template<>
struct TypeInfo<int64_t>
{
  typedef int64_t           StorageType;
  static constexpr DataType dataType  = DataType::INT64;
  static constexpr size_t   size      = sizeof(int64_t);
  static constexpr int      tupleSize = 1;
  static constexpr bool     isNumeric = true;
  static constexpr bool     isArray   = false;
  static constexpr bool     isTrivial = true;
  static constexpr char const* const name = "int64_t";
  static StorageType*       address(int64_t& v) { return &v; }
  static StorageType const* address(int64_t const& v) { return &v; }
};
template<>
struct TypeInfo<uint64_t>
{
  typedef int64_t           StorageType;
  static constexpr DataType dataType  = DataType::UINT64;
  static constexpr size_t   size      = sizeof(uint64_t);
  static constexpr int      tupleSize = 1;
  static constexpr bool     isNumeric = true;
  static constexpr bool     isArray   = false;
  static constexpr bool     isTrivial = true;
  static constexpr char const* const name = "uint64_t";
  static StorageType*       address(int64_t& v) { return &v; }
  static StorageType*       address(uint64_t& v) { return reinterpret_cast<StorageType*>(&v); }
  static StorageType const* address(uint64_t const& v)
  {
    return reinterpret_cast<StorageType const*>(&v);
  }
};
template<>
struct TypeInfo<float>
{
  typedef float             StorageType;
  static constexpr DataType dataType  = DataType::FLOAT;
  static constexpr size_t   size      = sizeof(float);
  static constexpr int      tupleSize = 1;
  static constexpr bool     isNumeric = true;
  static constexpr bool     isArray   = false;
  static constexpr bool     isTrivial = true;
  static constexpr char const* const name = "float";
  static StorageType*       address(float& v) { return &v; }
  static StorageType const* address(float const& v) { return &v; }
};
template<>
struct TypeInfo<double>
{
  typedef double            StorageType;
  static constexpr DataType dataType  = DataType::DOUBLE;
  static constexpr size_t   size      = sizeof(double);
  static constexpr int      tupleSize = 1;
  static constexpr bool     isNumeric = true;
  static constexpr bool     isArray   = false;
  static constexpr bool     isTrivial = true;
  static constexpr char const* const name = "double";
  static StorageType*       address(double& v) { return &v; }
  static StorageType const* address(double const& v) { return &v; }
};
template<glm::length_t N, typename T, glm::qualifier Q>
struct TypeInfo<glm::vec<N, T, Q>>
{
  typedef T                 StorageType;
  static constexpr DataType dataType  = TypeInfo<T>::dataType;
  static constexpr size_t   size      = sizeof(glm::vec<N, T, Q>);
  static constexpr int      tupleSize = N;
  static constexpr bool     isNumeric = true;
  static constexpr bool     isArray   = false;
  static constexpr bool     isTrivial = true;
  static constexpr char const* const name = "vec<>"; // TODO: make this compile-time literial
  static StorageType*       address(glm::vec<N, T, Q>& v) { return &v[0]; }
  static StorageType const* address(glm::vec<N, T, Q> const& v) { return &v[0]; }
};
template<typename T, glm::qualifier Q>
struct TypeInfo<glm::qua<T, Q>>
{
  typedef T                 StorageType;
  static constexpr DataType dataType  = TypeInfo<T>::dataType;
  static constexpr size_t   size      = sizeof(glm::qua<T, Q>);
  static constexpr int      tupleSize = 4;
  static constexpr bool     isNumeric = true;
  static constexpr bool     isArray   = false;
  static constexpr bool     isTrivial = true;
  static constexpr char const* const name = "qua<>"; // TODO: make this compile-time literial
  static StorageType*       address(glm::qua<T, Q>& q) { return &q[0]; }
  static StorageType const* address(glm::qua<T, Q> const& q) { return &q[0]; }
};
template<glm::length_t R, glm::length_t C, typename T, glm::qualifier Q>
struct TypeInfo<glm::mat<R, C, T, Q>>
{
  typedef T                 StorageType;
  static constexpr DataType dataType  = TypeInfo<T>::dataType;
  static constexpr size_t   size      = sizeof(T) * R * C;
  static constexpr int      tupleSize = R * C;
  static constexpr bool     isNumeric = true;
  static constexpr bool     isArray   = false;
  static constexpr bool     isTrivial = true;
  static constexpr char const* const name = "mat<>"; // TODO: make this compile-time literial
  static StorageType*       address(glm::mat<R, C, T, Q>& v) { return &v[0][0]; }
  static StorageType const* address(glm::mat<R, C, T, Q> const& v) { return &v[0][0]; }
};
template<typename T>
struct TypeInfo<T*>
{
  typedef T                 StorageType;
  static constexpr DataType dataType  = DataType::BLOB;
  static constexpr size_t   size      = 0;
  static constexpr int      tupleSize = 0;
  static constexpr bool     isNumeric = false;
  static constexpr bool     isArray   = true;
  static constexpr bool     isTrivial = false;
  static constexpr char const* const name = "T*"; // TODO: make this compile-time literial
  static StorageType*       address(T* v) { return v; }
  static StorageType const* address(T const* v) { return v; }
};
template<>
struct TypeInfo<void*>
{
  typedef byte              StorageType;
  static constexpr DataType dataType  = DataType::BLOB;
  static constexpr size_t   size      = 0;
  static constexpr int      tupleSize = 0;
  static constexpr bool     isNumeric = false;
  static constexpr bool     isArray   = true;
  static constexpr bool     isTrivial = false;
  static constexpr char const* const name = "void*";
  static StorageType*       address(void* v) { return static_cast<StorageType*>(v); }
  static StorageType const* address(void const* v) { return static_cast<StorageType const*>(v); }
};

// }}}

// Inverse Query {{{
template <DataType DT>
struct InvTypeInfo : public TypeInfo<void>
{ };
template <>
struct InvTypeInfo<DataType::INT32> : public TypeInfo<int32_t>
{ };
template <>
struct InvTypeInfo<DataType::UINT32> : public TypeInfo<uint32_t>
{ };
template <>
struct InvTypeInfo<DataType::INT64> : public TypeInfo<int64_t>
{ };
template <>
struct InvTypeInfo<DataType::UINT64> : public TypeInfo<uint64_t>
{ };
template <>
struct InvTypeInfo<DataType::FLOAT> : public TypeInfo<float>
{ };
template <>
struct InvTypeInfo<DataType::DOUBLE> : public TypeInfo<double>
{ };

constexpr inline size_t dataTypeSize(DataType dt)
{
  switch (dt) {
#define DATATYPE_CASE(DT) case DataType::DT: return InvTypeInfo<DataType::DT>::size
    DATATYPE_CASE(INT32);
    DATATYPE_CASE(UINT32);
    DATATYPE_CASE(INT64);
    DATATYPE_CASE(UINT64);
    DATATYPE_CASE(FLOAT);
    DATATYPE_CASE(DOUBLE);
    DATATYPE_CASE(STRUCTURE);
    DATATYPE_CASE(STRING);
    DATATYPE_CASE(BLOB);
  default:
    return 0;
#undef DATATYPE_CASE
  }
  return 0;
}

constexpr inline bool isNumeric(DataType dt)
{
  switch (dt) {
  case DataType::INT32:
  case DataType::UINT32:
  case DataType::INT64:
  case DataType::UINT64:
  case DataType::FLOAT:
  case DataType::DOUBLE:
    return true;
  default:
    return false;
  }
}

constexpr inline bool isTrivial(DataType dt)
{
  switch (dt) {
  case DataType::INT32:
  case DataType::UINT32:
  case DataType::INT64:
  case DataType::UINT64:
  case DataType::FLOAT:
  case DataType::DOUBLE:
  case DataType::STRUCTURE:
    return true;
  default:
    return false;
  }
}

constexpr inline char const* dataTypeName(DataType dt)
{
  switch (dt) {
#define DATATYPE_CASE(DT) case DataType::DT: return InvTypeInfo<DataType::DT>::name
    DATATYPE_CASE(INT32);
    DATATYPE_CASE(UINT32);
    DATATYPE_CASE(INT64);
    DATATYPE_CASE(UINT64);
    DATATYPE_CASE(FLOAT);
    DATATYPE_CASE(DOUBLE);
  case DataType::STRUCTURE:
    return "custom_struct_t";
  case DataType::STRING:
    return "string";
  case DataType::BLOB:
    return "blob";
  default:
    return "unknown";
#undef DATATYPE_CASE
  }
  return "unknown";
}
// Inverse Query }}}

END_JOYFLOW_NAMESPACE
