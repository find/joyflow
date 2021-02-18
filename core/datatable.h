#pragma once

#include "def.h"
#include "intrusiveptr.h"
#include "stats.h"
#include "stringview.h"
#include "traits.h"
#include "utility.h"
#include "vector.h"
#include "error.h"

#include <any>

BEGIN_JOYFLOW_NAMESPACE

class DataColumn;
typedef IntrusivePtr<DataColumn> DataColumnPtr;

// CellIndex {{{
/// Index into data column.
///
/// under the hood it's just size_t,
/// but it shouldn't be compared against raw
/// integers or been silently converted from
/// integers
///
/// And columns shouldn't be accessed
/// by raw integers either
class CellIndex
{
  size_t index_;

public:
  CellIndex() : index_(0) {}
  CellIndex(CellIndex const& ci) : index_(ci.index_) {}
  explicit CellIndex(size_t idx) : index_(idx) {}
  size_t value() const { return index_; }
  bool   valid() const { return index_ != -1; }

  // not all arithmetic operators are defined, belowing should be sufficient
  CellIndex& operator++()
  {
    ++index_;
    return *this;
  }
  CellIndex& operator--()
  {
    --index_;
    return *this;
  }
  CellIndex& operator+=(sint n)
  {
    index_ += n;
    return *this;
  }
  CellIndex& operator-=(sint n)
  {
    index_ -= n;
    return *this;
  }
  CellIndex operator+(sint n) { return CellIndex(index_ + n); }
  CellIndex operator-(sint n) { return CellIndex(index_ - n); }
  bool      operator==(CellIndex const& rhs) const { return index_ == rhs.index_; }
  bool      operator!=(CellIndex const& rhs) const { return index_ != rhs.index_; }
  bool      operator<(CellIndex const& rhs) const { return index_ < rhs.index_; }
  bool      operator>(CellIndex const& rhs) const { return index_ > rhs.index_; }
  bool      operator<=(CellIndex const& rhs) const { return index_ <= rhs.index_; }
  bool      operator>=(CellIndex const& rhs) const { return index_ >= rhs.index_; }
  bool      operator==(size_t rhs) const { return index_ == rhs; }
  bool      operator!=(size_t rhs) const { return index_ != rhs; }
  bool      operator<(size_t rhs) const { return index_ < rhs; }
  bool      operator>(size_t rhs) const { return index_ > rhs; }
  bool      operator<=(size_t rhs) const { return index_ <= rhs; }
  bool      operator>=(size_t rhs) const { return index_ >= rhs; }
};
inline bool operator==(size_t lhs, CellIndex const& rhs)
{
  return lhs == rhs.value();
}
inline bool operator!=(size_t lhs, CellIndex const& rhs)
{
  return lhs != rhs.value();
}
inline bool operator<(size_t lhs, CellIndex const& rhs)
{
  return lhs < rhs.value();
}
inline bool operator>(size_t lhs, CellIndex const& rhs)
{
  return lhs > rhs.value();
}
inline bool operator<=(size_t lhs, CellIndex const& rhs)
{
  return lhs <= rhs.value();
}
inline bool operator>=(size_t lhs, CellIndex const& rhs)
{
  return lhs >= rhs.value();
}
// CellIndex }}}

// Numeric Data Interface {{{
/// numeric data interface
/// for most common workload
class NumericDataInterface
{
public:
  virtual ~NumericDataInterface() {}

  virtual sint     tupleSize() const = 0;
  virtual DataType dataType() const = 0;

  virtual void const* getRawBufferRO(size_t offset, size_t count, DataType type) const = 0;
  virtual void*       getRawBufferRW(size_t offset, size_t count, DataType type) = 0;

  virtual bool getInt32Array(int32_t*  arrayToFill,
                             size_t&   outLength,
                             size_t    storageOffset,
                             size_t    count = -1) const = 0;
  virtual bool getUint32Array(uint32_t* arrayToFill,
                              size_t&   outLength,
                              size_t    storageOffset,
                              size_t    count = -1) const
  {
    return getInt32Array(reinterpret_cast<int32_t*>(arrayToFill), outLength, storageOffset, count);
  }
  virtual bool getInt64Array(int64_t*  arrayToFill,
                             size_t&   outLength,
                             size_t    storageOffset,
                             size_t    count = -1) const = 0;
  virtual bool getUint64Array(uint64_t* arrayToFill,
                              size_t&   outLength,
                              size_t    storageOffset,
                              size_t    count = -1) const
  {
    return getInt64Array(reinterpret_cast<int64_t*>(arrayToFill), outLength, storageOffset, count);
  }
  virtual bool getFloatArray(float*    arrayToFill,
                             size_t&   outLength,
                             size_t    storageOffset,
                             size_t    count = -1) const = 0;
  virtual bool getDoubleArray(double*   arrayToFill,
                              size_t&   outLength,
                              size_t    storageOffset,
                              size_t    count = -1) const = 0;

  virtual void setInt32Array(int32_t const* array, size_t storageOffset, size_t length) = 0;
  virtual void setUint32Array(uint32_t const* array, size_t storageOffset, size_t length)
  {
    setInt32Array(reinterpret_cast<int32_t const*>(array), storageOffset, length);
  }
  virtual void setInt64Array(int64_t const* array, size_t storageOffset, size_t length) = 0;
  virtual void setUint64Array(uint64_t const* array, size_t storageOffset, size_t length)
  {
    setInt64Array(reinterpret_cast<int64_t const*>(array), storageOffset, length);
  }
  virtual void setFloatArray(float const* array, size_t storageOffset, size_t length)   = 0;
  virtual void setDoubleArray(double const* array, size_t storageOffset, size_t length) = 0;

  // easy accessors {{{
  template<class T>
  T const* rawBufferRO(CellIndex index, size_t count)
  {
    if (TypeInfo<T>::dataType == dataType() && TypeInfo<T>::tupleSize == tupleSize()) {
      return static_cast<T const*>(getRawBufferRO(index.value()*tupleSize(), count*tupleSize(), TypeInfo<T>::dataType));
    } else {
      return nullptr;
    }
  }
  template<class T>
  T* rawBufferRW(CellIndex index, size_t count)
  {
    if (TypeInfo<T>::dataType == dataType() && TypeInfo<T>::tupleSize == tupleSize()) {
      return static_cast<T*>(getRawBufferRW(index.value()*tupleSize(), count*tupleSize(), TypeInfo<T>::dataType));
    } else {
      return nullptr;
    }
  }

  template<class T>
  T get(CellIndex index, sint tupleidx) const
  {
    throw TypeError(fmt::format("Cannot get data of type {} from numeric data interface", typeid(T).name()));
    return T{};
  }
  template<>
  int32_t get<int32_t>(CellIndex index, sint tupleidx) const
  {
    int32_t value = 0;
    size_t  len   = 0;
    getInt32Array(&value, len, index.value() * tupleSize() + tupleidx, 1);
    return value;
  }
  template<>
  uint32_t get<uint32_t>(CellIndex index, sint tupleidx) const
  {
    uint32_t value = 0;
    size_t   len   = 0;
    getUint32Array(&value, len, index.value() * tupleSize() + tupleidx, 1);
    return value;
  }
  template<>
  int64_t get<int64_t>(CellIndex index, sint tupleidx) const
  {
    int64_t value = 0;
    size_t  len   = 0;
    getInt64Array(&value, len, index.value() * tupleSize() + tupleidx, 1);
    return value;
  }
  template<>
  uint64_t get<uint64_t>(CellIndex index, sint tupleidx) const
  {
    uint64_t value = 0;
    size_t   len   = 0;
    getUint64Array(&value, len, index.value() * tupleSize() + tupleidx, 1);
    return value;
  }
  template<>
  float get<float>(CellIndex index, sint tupleidx) const
  {
    float  value = 0;
    size_t len   = 0;
    getFloatArray(&value, len, index.value() * tupleSize() + tupleidx, 1);
    return value;
  }
  template<>
  double get<double>(CellIndex index, sint tupleidx) const
  {
    double value = 0;
    size_t len   = 0;
    getDoubleArray(&value, len, index.value() * tupleSize() + tupleidx, 1);
    return value;
  }
  template<typename T>
  typename std::enable_if<TypeInfo<T>::isNumeric, T>::type get(CellIndex index) const
  {
    typedef typename TypeInfo<T>::StorageType value_type;

    T          ret{};
    size_t     len = 0;
    auto const ts  = std::min<sint>(TypeInfo<T>::tupleSize, tupleSize());
    bool       succeed = false;
    if constexpr (std::is_same<value_type, float>::value) {
      succeed = getFloatArray(
          TypeInfo<T>::address(ret), len, index.value() * tupleSize(), ts);
    } else if constexpr (std::is_same<value_type, double>::value) {
      succeed = getDoubleArray(
          TypeInfo<T>::address(ret), len, index.value() * tupleSize(), ts);
    } else if constexpr (std::is_same<value_type, int32_t>::value) {
      succeed = getInt32Array(
          TypeInfo<T>::address(ret), len, index.value() * tupleSize(), ts);
    } else if constexpr (std::is_same<value_type, int64_t>::value) {
      succeed = getInt64Array(
          TypeInfo<T>::address(ret), len, index.value() * tupleSize(), ts);
    } else {
      throw TypeError("unsupported glm::vec type for get<>() interface");
    }
    ASSERT(succeed);
    return ret;
  }

  template<class T>
  void set(CellIndex index, T value, sint tupleidx)
  {
    throw TypeError(fmt::format("Cannot set data of type {} to numeric data interface", typeid(T).name()));
  }
  template<>
  void set<int32_t>(CellIndex index, int32_t value, sint tupleidx)
  {
    setInt32Array(&value, index.value() * tupleSize() + tupleidx, 1);
  }
  template<>
  void set<uint32_t>(CellIndex index, uint32_t value, sint tupleidx)
  {
    setUint32Array(&value, index.value() * tupleSize() + tupleidx, 1);
  }
  template<>
  void set<int64_t>(CellIndex index, int64_t value, sint tupleidx)
  {
    setInt64Array(&value, index.value() * tupleSize() + tupleidx, 1);
  }
  template<>
  void set<uint64_t>(CellIndex index, uint64_t value, sint tupleidx)
  {
    setUint64Array(&value, index.value() * tupleSize() + tupleidx, 1);
  }
  template<>
  void set<float>(CellIndex index, float value, sint tupleidx)
  {
    setFloatArray(&value, index.value() * tupleSize() + tupleidx, 1);
  }
  template<>
  void set<double>(CellIndex index, double value, sint tupleidx)
  {
    setDoubleArray(&value, index.value() * tupleSize() + tupleidx, 1);
  }
  template<class T>
  typename std::enable_if<TypeInfo<T>::isNumeric, void>::type set(CellIndex index, T const& v)
  {
    RUNTIME_CHECK(TypeInfo<T>::tupleSize <= tupleSize(), "tupleSize mismatch, expected to be <={}, got {}", tupleSize(), TypeInfo<T>::tupleSize);
    typedef typename TypeInfo<T>::StorageType value_type;

    auto const ts = TypeInfo<T>::tupleSize;
    if constexpr (std::is_same<value_type, float>::value) {
      setFloatArray(
          TypeInfo<T>::address(v), index.value() * tupleSize(), ts);
    } else if constexpr (std::is_same<value_type, double>::value) {
      setDoubleArray(
          TypeInfo<T>::address(v), index.value() * tupleSize(), ts);
    } else if constexpr (std::is_same<value_type, int32_t>::value) {
      setInt32Array(
          TypeInfo<T>::address(v), index.value() * tupleSize(), ts);
    } else if constexpr (std::is_same<value_type, int64_t>::value) {
      setInt64Array(
          TypeInfo<T>::address(v), index.value() * tupleSize(), ts);
    } else {
      throw TypeError("unsupported glm::vec type for set<>() interface");
    }
  }

  // }}}
};

// }}} Numeric Data Interface

// Fix-sized Structured Data Interface {{{
/// structured data interface
/// for fixed length POD data
class FixSizedDataInterface
{
public:
  virtual ~FixSizedDataInterface() {}

  virtual size_t itemSize() const = 0;

  virtual bool getItems(void*     outItems,
                        size_t&   outCount,
                        CellIndex startIndex,
                        size_t    count) const = 0;

  virtual bool setItems(void const* inItems, CellIndex startIndex, size_t count) = 0;

  virtual void setToStringMethod(String(*toString)(void const*)) = 0;

  template<typename T>
  std::enable_if_t<std::is_trivial<T>::value, T>
  get(CellIndex itemIndex)
  {
    RUNTIME_CHECK(sizeof(T) == itemSize(), "Structure size mismatch: got {}, expect {}", sizeof(T), itemSize());
    T      item;
    size_t len;
    getItems(&item, len, itemIndex, 1);
    return item;
  }
  template<typename T>
  std::enable_if_t<std::is_trivial<T>::value, void>
  set(CellIndex itemIndex, T const& v)
  {
    RUNTIME_CHECK(sizeof(T) == itemSize(), "Structure size mismatch: got {}, expect {}", sizeof(T), itemSize());
    setItems(&v, itemIndex, 1);
  }
};

// }}} Structured Data Interface

// Variable Sized Block Data Interface {{{
/// for var size data
/// like string or array

struct SharedBlob : public ReferenceCounted<SharedBlob>, ObjectTracker<SharedBlob>
{
  void const* data = nullptr;
  size_t      size = 0;
  size_t      hash = 0;
  void* operator new(size_t size) { return Allocator::malloc(size); }
  void operator delete(void* ptr) { Allocator::free(ptr); }
  SharedBlob(void const* data, size_t size, size_t hash = 0)
  {
    if (!!data && size) {
      this->data = Allocator::malloc(size);
      this->size = size;
      this->hash = hash ? hash : xxhash(data, size);
      if (data) {
        memcpy(const_cast<void*>(this->data), data, size);
      }
    } else {
      this->data = nullptr;
      this->size = 0;
      this->hash = 0;
    }
  }
  ~SharedBlob() { Allocator::free(const_cast<void*>(this->data)); }
};
typedef IntrusivePtr<SharedBlob> SharedBlobPtr;

class BlobDataInterface
{
public:
  virtual ~BlobDataInterface() {}

public:
  virtual bool   setBlobData(CellIndex index, void const* data, size_t size) = 0;
  virtual size_t getBlobSize(CellIndex index) const                          = 0;
  /// the raw pointer would be invalid once defragment() was called
  virtual bool getBlobData(CellIndex index, void* data, size_t& size) const = 0;

  virtual bool          setBlob(CellIndex index, SharedBlobPtr blob) = 0;
  virtual SharedBlobPtr getBlob(CellIndex index) const               = 0;

  template<typename T>
  bool setBlob(CellIndex index, T const* data, size_t count)
  {
    return setBlobData(index, data, count * sizeof(T));
  }
  template<typename T>
  bool getBlob(CellIndex index, T* data, size_t& count) const
  {
    bool succeed = getBlobData(index, data, count);
    count /= sizeof(T);
    return succeed;
  }
};

// }}} Block Data Interface

// String Data Interface {{{
class StringDataInterface
{
public:
  virtual ~StringDataInterface() {}

  virtual bool       setString(CellIndex index, StringView const& str) = 0;
  virtual StringView getString(CellIndex index) const                  = 0;
};
// }}}

// Container Interface {{{
class VectorDataInterface
{
public:
  virtual ~VectorDataInterface() {}

  virtual DataType dataType() const                = 0;
  virtual sint     tupleSize() const               = 0;
  virtual void*    data(CellIndex index)           = 0;
  virtual size_t   size(CellIndex index) const     = 0;
  virtual size_t   capacity(CellIndex index) const = 0;

  template<class T>
  Vector<T>* asVector(CellIndex index)
  {
    Vector<T>* vec = nullptr;
    if (TypeInfo<T>::dataType != dataType() || TypeInfo<T>::tupleSize != tupleSize() ||
        TypeInfo<T>::isTrivial == false) {
      return nullptr;
    }
    return reinterpret_cast<Vector<T>*>(rawVectorPtr(index));
  }
  template<class T>
  Vector<T> const* asVector(CellIndex index) const
  {
    return const_cast<Vector<T> const*>(
        const_cast<VectorDataInterface*>(this)->asVector<T>(index));
  }

  virtual Vector<byte>* rawVectorPtr(CellIndex index) = 0;
  virtual Vector<byte> const* rawVectorPtr(CellIndex index) const
  {
    return const_cast<VectorDataInterface*>(this)->rawVectorPtr(index);
  }
};
// }}}

// Copy Interface {{{
class CopyInterface
{
public:
  virtual bool copyable(DataColumn const* that) const = 0;

  // this[a] = this[b]
  virtual bool copy(CellIndex a, CellIndex b) = 0;

  // this[a..a+n] = this[b..b+n]
  virtual bool copy(CellIndex a, CellIndex b, size_t n)
  {
    bool succeed = true;
    for (size_t i=0;i<n;++i)
      succeed &= copy(a+i, b+i);
    return succeed;
  }

  // this[indexInThis] = that[indexInThat]
  virtual bool copy(CellIndex indexInThis, DataColumn const* that, CellIndex indexInThat) = 0;

  // this[indexInThis] = that[indexInThat]
  virtual bool copy(CellIndex indexInThis, DataColumn const* that, CellIndex indexInThat, size_t n) 
  {
    bool succeed = true;
    for (size_t i=0;i<n;++i)
      succeed &= copy(indexInThis+i, that, indexInThat+i);
    return succeed;
  }
};
// }}}

// Compare Interface {{{
class CompareInterface
{
public:
  virtual bool comparable(DataColumn const* that) const = 0;
  virtual bool searchable(DataType dataType, sint tupleSize, size_t size) const = 0;

  /// return -1 if this[a] <  this[b]
  ///         0 if this[a] == this[b]
  ///         1 if this[a] >  this[b]
  virtual int  compare(CellIndex a, CellIndex b) const = 0;

  /// return -1 if this[indexInThis] <  that[indexInThat]
  ///         0 if this[indexInThis] == that[indexInThat]
  ///         1 if this[indexInThis] >  that[indexInThat]
  virtual int  compare(CellIndex indexInThis, DataColumn const* that, CellIndex indexInThat) const = 0;

  template <class T>
  bool searchable() const
  {
    return searchable(TypeInfo<T>::dataType, TypeInfo<T>::tupleSize, TypeInfo<T>::size);
  }

  virtual CellIndex search(DataTable const* habitat, DataType dataType, void const* data, size_t size) const = 0;
  virtual size_t searchAll(Vector<CellIndex>& outMatches, DataTable const* habitat, DataType dataType, void const* data, size_t size) const = 0;

  template <class T>
  CellIndex search(DataTable const* habitat, T const& v) const
  {
    DEBUG_ASSERT(searchable<T>());
    return search(habitat, TypeInfo<T>::dataType, &v, TypeInfo<T>::size);
  }

  template <class T>
  size_t searchAll(Vector<CellIndex>& outMatches, DataTable const* habitat, T const& v) const
  {
    DEBUG_ASSERT(searchable<T>());
    return searchAll(outMatches, habitat, TypeInfo<T>::dataType, &v, TypeInfo<T>::size);
  }

  template <class T>
  Vector<CellIndex> searchAll(DataTable const* habitat, T const& v) const
  {
    Vector<CellIndex> ret;
    searchAll<T>(ret, habitat, v);
    return ret;
  }

  static CompareInterface* notComparable(); /// global instance of non-comparable relationship
};
// }}}

// Math Interface {{{
class MathInterface
{
public:
  /// binary ops has 8 forms:
  ///
  /// e.g. for add:
  /// addTo(a,b):
  ///   this[a] += this[b]
  ///
  /// addTo(a, that, b):
  ///   this[a] += that[b]
  ///
  /// addTo(a, real val):
  ///   this[a] += val
  ///
  /// addTo(a, sint val):
  ///   this[a] += val
  ///
  /// add(lhs, a,b):
  ///   this[lhs] = this[a] + this[b]
  ///
  /// add(lhs, a, that, b):
  ///   this[lhs] = this[a] + that[b]
  ///
  /// add(lhs, that, a, b):
  ///   this[lhs] = that[a] + this[b]
  ///
  /// add(lhs, X, a, Y, b):
  ///   this[lhs] = X[a] + Y[b]
#define DEFINE_BINARY_MATHOP(opname) \
  virtual bool opname##To(CellIndex a, CellIndex b) { return opname(a, a, b); } \
  virtual bool opname##To(CellIndex a, DataColumn const* that, CellIndex b) { return opname(a, a, that, b); } \
  virtual bool opname##To(CellIndex a, real val) = 0; \
  virtual bool opname##To(CellIndex a, sint val) = 0; \
  virtual bool opname(CellIndex lhs, CellIndex a, CellIndex b) = 0; \
  virtual bool opname(CellIndex lhs, CellIndex a, DataColumn const* that, CellIndex b) = 0; \
  virtual bool opname(CellIndex lhs, DataColumn const* that, CellIndex a, CellIndex b) = 0; \
  virtual bool opname(CellIndex lhs, DataColumn const* X, CellIndex a, DataColumn const* Y, CellIndex b) = 0

  DEFINE_BINARY_MATHOP(add);
  DEFINE_BINARY_MATHOP(sub);
  DEFINE_BINARY_MATHOP(mul);
  DEFINE_BINARY_MATHOP(div);
#undef DEFINE_BINARY_MATHOP

  /// this[a] = lerp(this[a], this[b], t)
  virtual bool lerpTo(CellIndex a, CellIndex b, real t) { return lerp(a, a, b, t); }
  /// this[a] = lerp(this[a], that[b], t)
  virtual bool lerpTo(CellIndex a, DataColumn const* that, CellIndex b, real t) { return lerp(a, a, that, b, t); }
  /// this[lhs] = lerp(this[a], this[b], t)
  virtual bool lerp(CellIndex lhs, CellIndex a, CellIndex b, real t) = 0;
  /// this[lhs] = lerp(this[a], that[b], t)
  virtual bool lerp(CellIndex lhs, CellIndex a, DataColumn const* that, CellIndex b, real t) = 0;
  /// this[lhs] = lerp(that[a], this[b], t)
  virtual bool lerp(CellIndex lhs, DataColumn const* that, CellIndex a, CellIndex b, real t) = 0;
  /// this[lhs] = lerp(X[a], Y[b], t)
  virtual bool lerp(CellIndex lhs, DataColumn const* X, CellIndex a, DataColumn const* Y, CellIndex b, real t) = 0;

};
// Math Interface }}}

// Data Column {{{
class DefragmentInfo;

// description {{{
/// to store None-POD objects in datacolumn, implement this
struct ObjectElementCallback
{
  String typeName                            = "unknown"; //< type name
  bool   (*isa)(void const* ptr)             = nullptr;   //< is the ptr pointing to a thing of
                                                          //  matching type?
  bool   (*copy)(void* dst, void const* src) = nullptr;   //< copy src to dst
  bool   (*move)(void* dst, void* src)       = nullptr;   //< move src to dst, invalidate src
  bool   (*destroy)(void* elem)              = nullptr;   //< destroys the elem
  String (*toSting)(void const* elem, sint lengthLimit)  = nullptr; // convert elem to string
  bool   (*fromSting)(void* elem, StringView const& str) = nullptr; // convert string to elem
};

/// DataColumnDesc describes how the datacolumn should be created
struct DataColumnDesc
{
  DataType               dataType    = DataType::UNKNOWN; //< underlying data type.
                                                          //  you can make up your own by adding
                                                          //  magic number to DataType::CUSTOM
  sint                   tupleSize   = 1;       //< number of components in each element
                                                //  (e.g. tupleSize==3 for vec3)
  size_t                 elemSize    = 0;       //< size of the individual element in this column
  bool                   dense       = true;    //< is the column holding dense data? (else sparse)
  bool                   fixSized    = true;    //< are elements in this column fixed size?
  bool                   container   = false;   //< are elements in this column containers?
  ObjectElementCallback *objCallback = nullptr; //< object callbacks. if not null, the elements
                                                //  will be treated as non-POD, and callbacks will
                                                //  be called at appropriate time
                                                //  NOTE: only fix-sized elements support this
  Vector<byte>           defaultValue;          //< data block of default value for the elements,
                                                //  can be empty
  CORE_API bool          isValid() const;       //< check if the desc is valid
  CORE_API bool          compatible(DataColumnDesc const& that) const; //< this is compatible with that?
};
inline bool operator==(DataColumnDesc const& a, DataColumnDesc const& b)
{
  return a.dataType == b.dataType &&
    a.tupleSize == b.tupleSize &&
    a.elemSize == b.elemSize &&
    a.dense == b.dense &&
    a.fixSized == b.fixSized &&
    a.container == b.container &&
    a.objCallback == b.objCallback &&
    a.defaultValue.size() == b.defaultValue.size() &&
    std::memcmp(a.defaultValue.data(), b.defaultValue.data(), a.defaultValue.size()) == 0;
}

// numeric data & trivial structs:
template <class T>
inline typename std::enable_if<
  TypeInfo<T>::isNumeric ||
  TypeInfo<T>::isTrivial,
  DataColumnDesc>::type
makeDataColumnDesc(T const& defaultValue={})
{
  Vector<byte> defVal(sizeof(T));
  memcpy(defVal.data(), &defaultValue, sizeof(T));
  DataColumnDesc desc = {
    TypeInfo<T>::dataType,
    TypeInfo<T>::tupleSize,
    sizeof(T),
    true, // TODO: sparse data
    true,
    false,
    nullptr,
    defVal
  };
  return desc;
}
// string columns:
template <class T>
inline typename std::enable_if<
  std::is_same<String, T>::value ||
  std::is_same<StringView, T>::value,
  DataColumnDesc>::type
makeDataColumnDesc(StringView const& sv = "")
{
  DataColumnDesc desc = {
    DataType::STRING,
    0,
    0,
    true,
    false, // fix size?
    false, // container?
    nullptr,
    Vector<byte>(sv.data(), sv.data()+sv.size())
  };
  return desc;
}
// blob columns:
template <class T>
inline typename std::enable_if<
  std::is_same<SharedBlob, T>::value,
  DataColumnDesc>::type
makeDataColumnDesc()
{
  DataColumnDesc desc = {
    DataType::BLOB,
    0,
    0,
    true,
    false, // fix size?
    false, // container?
    nullptr,
    {}
  };
  return desc;
}
// vector columns:
template <class T>
inline typename std::enable_if<
  //std::is_same<typename std::decay<typename std::result_of<decltype(&T::front)>::type>::type, T>::value && // looks like a vector
  T::tag_is_vector::value && // T is a vector
  TypeInfo<typename T::value_type>::isNumeric, // and stores numeric data
  DataColumnDesc>::type
makeDataColumnDesc()
{
  typedef typename T::value_type value_type;
  DataColumnDesc desc = {
    TypeInfo<value_type>::dataType,
    TypeInfo<value_type>::tupleSize,
    TypeInfo<value_type>::size,
    true,
    true, // fix size?
    true, // container?
    nullptr,
    {}
  };
  return desc;
}
// description }}}

/// the interface of data storage
/// designed to be sharable
class DataColumn : public ReferenceCounted<DataColumn>
{
protected:
  String         name_;
  DataColumnDesc desc_;

protected:
  DataColumn(String const& name, DataColumnDesc const& desc)
      : name_(name), desc_(desc)
  {}

private:
  DataColumn(DataColumn const& rhs) = delete;
  DataColumn(DataColumn&& rhs)      = delete;
  template<class Whatever>
  DataColumn& operator=(Whatever const&)
  {
    throw TypeError("please do not copy DataColumn!");
    return *this;
  }

public:
  virtual ~DataColumn() {}

  DataType              dataType() const { return desc_.dataType; }
  sint                  tupleSize() const { return desc_.tupleSize; }
  String                name() const { return name_; }
  void                  rename(String name) { name_ = std::move(name); }
  DataColumnDesc const& desc() const { return desc_; }

  /// number of items in this column
  virtual size_t                   length() const = 0;

  /// reserve space for `length` objects
  virtual void                     reserve(size_t length) = 0;

  virtual NumericDataInterface*    asNumericData() { return nullptr; }
  virtual FixSizedDataInterface*   asFixSizedData() { return nullptr; }
  virtual BlobDataInterface*       asBlobData() { return nullptr; }
  virtual StringDataInterface*     asStringData() { return nullptr; }
  virtual VectorDataInterface*     asVectorData() { return nullptr; }
  virtual MathInterface*           mathInterface() { return nullptr; }
  virtual CopyInterface*           copyInterface() { return nullptr; }
  virtual CompareInterface const*  compareInterface() const
  {
    return CompareInterface::notComparable();
  }

  NumericDataInterface const* asNumericData() const
  {
    return const_cast<NumericDataInterface const*>(
        const_cast<DataColumn*>(this)->asNumericData());
  }
  FixSizedDataInterface const* asFixSizedData() const
  {
    return const_cast<FixSizedDataInterface const*>(
        const_cast<DataColumn*>(this)->asFixSizedData());
  }
  BlobDataInterface const* asBlobData() const
  {
    return const_cast<BlobDataInterface const*>(
        const_cast<DataColumn*>(this)->asBlobData());
  }
  StringDataInterface const* asStringData() const
  {
    return const_cast<StringDataInterface const*>(
        const_cast<DataColumn*>(this)->asStringData());
  }
  VectorDataInterface const* asVectorData() const
  {
    return const_cast<VectorDataInterface const*>(
        const_cast<DataColumn*>(this)->asVectorData());
  }

  /// deep copy
  virtual DataColumnPtr clone() const = 0;
  /// COW shallow copy
  virtual DataColumnPtr share() const = 0;
  /// make own copy of data
  virtual void makeUnique() = 0;
  /// is my data unshared?
  virtual bool isUnique() const = 0;
  /// how many times my data was shared?
  virtual size_t shareCount() const = 0;
  /// use defragment info to perform the defragment operation
  virtual void defragment(DefragmentInfo const& how) = 0;

  /// merge another column in
  ///
  /// return `this` if the joining can be performed locally
  /// else return the newly created column
  /// 
  /// RULES:
  ///
  /// if our type is not compatible with their type :
  ///   do nothing
  /// if their storage is empty and default value is equal to ours:
  ///   do nothing
  /// if their data format is higher precision / dimension:
  ///   convert to their data format
  /// else:
  ///   fill up storage's back
  ///   concat storages
  ///   fill up the back of storage if their default value
  ///   does not equal to ours
  virtual DataColumn* join(DataColumn const* that) = 0;

  /// move `count` items from `src` index to `dst` index.
  /// after move, the original space will be filled with the column's
  /// default value (if exists)
  /// this operation is performed when joining tables together and
  /// during defragmentation
  virtual void move(CellIndex dst, CellIndex src, size_t count) = 0;

  /// memory statistics
  virtual void countMemory(size_t& sharedBytes, size_t& unsharedBytes) const = 0;

public:
  /// preview data
  virtual String toString(CellIndex index, sint lengthLimit=-1) const = 0;

  /// get cell data
  template<class T>
  typename std::enable_if<
    TypeInfo<T>::isNumeric || TypeInfo<T>::isTrivial
  , T>::type
  get(CellIndex index)
  {
    if constexpr (TypeInfo<T>::isNumeric) {
      auto* ni = asNumericData();
      RUNTIME_CHECK(ni, "No numeric interface for column \"{}\"", name_);
      return ni->get<T>(index);
    } else if constexpr (TypeInfo<T>::isTrivial) {
      auto* si = asFixSizedData();
      RUNTIME_CHECK(si, "No structure interface for column \"{}\"", name_);
      RUNTIME_CHECK(si->itemSize()==TypeInfo<T>::size, "Struct size mismatch of column \"{}\"", name_);
      return si->get<T>(index);
    } else {
      throw TypeError("unsupported type for get<> interface");
    }
  }
  template<class T>
  typename std::enable_if<std::is_same<StringView, T>::value, T>::type get(CellIndex index)
  {
    RUNTIME_CHECK(asStringData(), "No string interface for column \"{}\"", name_);
    return asStringData()->getString(index);
  }
  template<class T>
  typename std::enable_if<std::is_same<String, T>::value, T>::type get(CellIndex index)
  {
    RUNTIME_CHECK(asStringData(), "No string interface for column \"{}\"", name_);
    return String(asStringData()->getString(index));
  }
  template<class T>
  typename std::enable_if<std::is_same<SharedBlobPtr, T>::value, T>::type get(CellIndex index)
  {
    RUNTIME_CHECK(asBlobData(), "No blob interface for column \"{}\"", name_);
    return asBlobData()->getBlob(index);
  }

  /// get cell data
  template<class T>
  typename std::enable_if<TypeInfo<T>::isNumeric && TypeInfo<T>::tupleSize==1, T>::type
  get(CellIndex index, sint tupleidx)
  {
    auto* ni = asNumericData();
    RUNTIME_CHECK(ni, "No numeric interface for column \"{}\"", name_);
    RUNTIME_CHECK(tupleidx>=0 && tupleidx<tupleSize(), "tuple index {} should be in range [0, {})", tupleidx, tupleSize());
    return ni->get<T>(index, tupleidx);
  }

  /// set cell data
  template<class T>
  typename std::enable_if<
    TypeInfo<T>::isNumeric ||
    TypeInfo<T>::isTrivial ||
    std::is_same<String, T>::value ||
    std::is_same<StringView, T>::value ||
    std::is_same<SharedBlobPtr, T>::value ||
    std::is_same<SharedBlob*, T>::value
  , void>::type
  set(CellIndex index, T const& value)
  {
    if constexpr (TypeInfo<T>::isNumeric) {
      auto* ni = asNumericData();
      RUNTIME_CHECK(ni, "No numeric interface for column \"{}\"", name_);
      ni->set<T>(index, value);
    } else if constexpr (TypeInfo<T>::isTrivial) {
      auto* si = asFixSizedData();
      RUNTIME_CHECK(si, "No structure interface for column \"{}\"", name_);
      RUNTIME_CHECK(si->itemSize()==TypeInfo<T>::size,
          "Struct size mismatch of column \"{}\"", name_);
      si->set<T>(index, value);
    } else {
      throw TypeError("unsupported type for get<> interface");
    }
  }
  template<>
  void set<String>(CellIndex index, String const& str)
  {
    RUNTIME_CHECK(asStringData(), "No string interface for column \"{}\"", name_);
    asStringData()->setString(index, str);
  }
  template<>
  void set<StringView>(CellIndex index, StringView const& str)
  {
    RUNTIME_CHECK(asStringData(), "No string interface for column \"{}\"", name_);
    asStringData()->setString(index, str);
  }
  template<>
  void set<SharedBlobPtr>(CellIndex index, SharedBlobPtr const& blob)
  {
    RUNTIME_CHECK(asBlobData(), "No blob interface for column \"{}\"", name_);
    asBlobData()->setBlob(index, blob);
  }
  template<>
  void set<SharedBlob *>(CellIndex index, SharedBlob* const& blob)
  {
    RUNTIME_CHECK(asBlobData(), "No blob interface for column \"{}\"", name_);
    asBlobData()->setBlob(index, SharedBlobPtr(blob));
  }
  /// set cell data
  template<class T>
  void set(CellIndex index, T const& value, sint tupleidx)
  {
    if constexpr (TypeInfo<T>::isNumeric) {
      auto* ni = asNumericData();
      RUNTIME_CHECK(ni, "No numeric interface for column \"{}\"", name_);
      RUNTIME_CHECK(tupleidx>=0 && tupleidx<tupleSize(),
          "tuple index {} should be in range [0, {})", tupleidx, tupleSize());
      ni->set<T>(index, value, tupleidx);
    } else {
      throw TypeError("unsupported type for get<> interface");
    }
  }
};

// }}} Data Column

// Data Table {{{

/// DataTable holds multiple columns
class DataTable;
typedef IntrusivePtr<DataTable> DataTablePtr;

class DataTable : public ReferenceCounted<DataTable>
{
public:
  virtual ~DataTable() {}

  virtual sint numColumns() const = 0;
  virtual Vector<String> columnNames() const = 0;

  virtual DataColumn* getColumn(String const& name) = 0;
  virtual DataColumn const* getColumn(String const& name) const = 0;

  virtual DataColumn* setColumn(String const& name, DataColumn* col) = 0;
  
  virtual DataColumn* createColumn(String const& name,
                                   DataColumnDesc const& desc,
                                   bool          overwriteExisting = false) = 0;

  template<class T>
  DataColumn*
  createColumn(String const& name, T const& defaultValue, bool overwriteExisting = false)
  {
    auto desc = makeDataColumnDesc<T>(defaultValue);
    return createColumn(name,
                        desc,
                        overwriteExisting);
  }

  template<class T>
  DataColumn*
  createColumn(String const& name, bool overwriteExisting = false)
  {
    auto desc = makeDataColumnDesc<T>();
    return createColumn(name,
                        desc,
                        overwriteExisting);
  }

  virtual bool renameColumn(String const& oldName, String const& newName, bool overwriteExisting = true) = 0;
  virtual bool removeColumn(String const& name) = 0;

  /// get cell data
  template<class T>
  T get(String const& column, sint row)
  {
    auto* pcolumn = getColumn(column);
    ALWAYS_ASSERT(pcolumn);
    return pcolumn->get<T>(getIndex(row));
  }

  /// get cell data
  template<class T>
  T get(String const& column, sint row, sint tupleidx)
  {
    auto* pcolumn = getColumn(column);
    ALWAYS_ASSERT(pcolumn);
    return pcolumn->get<T>(getIndex(row), tupleidx);
  }

  /// get cell data
  template<class T>
  T get(String const& column, CellIndex index)
  {
    auto* pcolumn = getColumn(column);
    ALWAYS_ASSERT(pcolumn);
    return pcolumn->get<T>(index);
  }

  /// get cell data
  template<class T>
  T get(String const& column, CellIndex index, sint tupleidx)
  {
    auto* pcolumn = getColumn(column);
    ALWAYS_ASSERT(pcolumn);
    return pcolumn->get<T>(index, tupleidx);
  }

  /// set cell data
  template<class T>
  void set(String const& column, sint row, T const& value)
  {
    auto* pcolumn = getColumn(column);
    ALWAYS_ASSERT(pcolumn);
    pcolumn->set<T>(getIndex(row), value);
  }

  /// set cell data
  template<class T>
  void set(String const& column, sint row, T const& value, sint tupleidx)
  {
    auto* pcolumn = getColumn(column);
    ALWAYS_ASSERT(pcolumn);
    pcolumn->set<T>(getIndex(row), value, tupleidx);
  }

  /// set cell data
  template<class T>
  void set(String const& column, CellIndex index, T const& value)
  {
    auto* pcolumn = getColumn(column);
    ALWAYS_ASSERT(pcolumn);
    pcolumn->set<T>(index, value);
  }

  /// set cell data
  template<class T>
  void set(String const& column, CellIndex index, T const& value, sint tupleidx)
  {
    auto* pcolumn = getColumn(column);
    ALWAYS_ASSERT(pcolumn);
    pcolumn->set<T>(index, value, tupleidx);
  }

  /// append one row for each column of this table
  /// return the cell index corresponding to this newly added row
  virtual CellIndex addRow() = 0;

  /// append n rows for each column of this table
  /// the indices of these new rows are guaranteed to be continuous
  /// return the first cell index corresponding to these newly added rows
  virtual CellIndex addRows(size_t n) = 0;

  /// mark row invalid, need to delete it later by calling `removeMarkedRows`
  virtual void markRemoval(sint row) = 0;

  /// delete rows marked above
  virtual void applyRemoval() = 0;

  /// remove one row from this table immediately
  virtual void removeRow(sint row) = 0;

  /// remove n continuous rows from this table, till the end
  /// return number of rows really got deleted
  virtual size_t removeRows(sint row, size_t n) = 0;

  /// get internal index of row number
  virtual CellIndex getIndex(sint row) const = 0;

  /// get row number from internal index
  virtual sint getRow(CellIndex index) const = 0;

  virtual size_t numRows() const = 0;

  virtual size_t numIndices() const = 0;

  /// defragment: remove holes and make data packed dense
  virtual void defragment() = 0;

  /// sort to given order
  virtual void sort(Vector<sint> const& order) = 0;

  /// make a readonly shared copy of this table
  virtual DataTablePtr share() = 0;

  /// am I shared?
  virtual bool isUnique() const = 0;

  /// how many times is my content shared?
  virtual size_t shareCount() const = 0;

  /// make unique copy of column references and index map
  ///
  /// after this operation, it'll be safe to:
  /// 1) delete columns from table
  /// 2) delete rows from table
  /// 3) rename columns from table
  /// 4) sort rows
  /// 5) create new columns and modify their content
  ///
  /// following operations are still unsafe, untill making
  /// of unique copy for each column:
  /// 1) defragment
  /// 2) join tables
  /// 3) set column data
  /// 4) add rows
  virtual void makeUnique() = 0;

  /// join two tables together, `this` before that
  /// `this` should be unique
  virtual void join(DataTable const* that) = 0;

  /// memory statistics
  virtual void countMemory(size_t& sharedBytes, size_t& unsharedBytes) const = 0;

  /// meta
  virtual HashMap<String, std::any> const& vars() const = 0;
  virtual void setVariable(String const& key, std::any const& val) = 0;
  virtual std::any getVariable(String const& key) const = 0;
};
// Data Table }}}

// Data Collection API {{{

class DataCollection;
typedef IntrusivePtr<DataCollection> DataCollectionPtr;

CORE_API DataCollectionPtr newDataCollection();
CORE_API void              deleteDataCollection(DataCollection* dc);

// this ensures allocation and free of DataCollection object is managed by ourself
class DataCollectionDeletor
{
public:
  void operator()(DataCollection* dc) const { deleteDataCollection(dc); }
};

/// DataCollection: a worksheet-like interface of data storage
/// DataCollection hold multiple DataTables
class DataCollection : public ReferenceCounted<DataCollection, DataCollectionDeletor>
{
public:
  virtual ~DataCollection() {}

  virtual sint addTable()              = 0;
  virtual sint addTable(DataTable* dt) = 0;
  virtual void reserveTables(sint n)   = 0;
  virtual void removeTable(sint table) = 0;

  virtual sint             numTables() const          = 0;
  virtual DataTable*       getTable(sint table)       = 0;
  virtual DataTable const* getTable(sint table) const = 0;

  /// defragment: try to make ids and indices identical
  virtual void defragment() = 0;

  /// make an COW copy
  virtual DataCollectionPtr share() = 0;

  virtual void join(DataCollection const* that) = 0;

  virtual void countMemory(size_t& sharedBytes, size_t& unsharedBytes) const = 0;

  // easy accessors {{{

  DataColumn* getColumn(sint table, String const& name)
  {
    auto* ptab = getTable(table);
    ALWAYS_ASSERT(ptab);
    return ptab->getColumn(name);
  }

  bool renameColumn(sint table, String const& oldName, String const& newName, bool overwriteExisting = true)
  {
    RUNTIME_CHECK(table>=0 && table<numTables(), "table {} out of bound [0, {})", table, 0, numTables());
    return getTable(table)->renameColumn(oldName, newName, overwriteExisting);
  }

  bool removeColumn(sint table, String const& name)
  {
    RUNTIME_CHECK(table>=0 && table<numTables(), "table {} out of bound [0, {})", table, 0, numTables());
    return getTable(table)->removeColumn(name);
  }

  /// get cell data
  template<class T>
  T get(sint table, String const& column, sint row)
  {
    ALWAYS_ASSERT(table >= 0 && table < numTables());
    auto* pcolumn = getColumn(table, column);
    ALWAYS_ASSERT(pcolumn);
    return pcolumn->get<T>(getIndex(table, row));
  }

  /// get cell data
  template<class T>
  T get(sint table, String const& column, sint row, sint tupleidx)
  {
    ALWAYS_ASSERT(table >= 0 && table < numTables());
    auto* pcolumn = getColumn(table, column);
    ALWAYS_ASSERT(pcolumn);
    return pcolumn->get<T>(getIndex(table, row), tupleidx);
  }

  /// get cell data
  template<class T>
  T get(sint table, String const& column, CellIndex index)
  {
    ALWAYS_ASSERT(table >= 0 && table < numTables());
    auto* pcolumn = getColumn(table, column);
    ALWAYS_ASSERT(pcolumn);
    return pcolumn->get<T>(index);
  }

  /// get cell data
  template<class T>
  T get(sint table, String const& column, CellIndex index, sint tupleidx)
  {
    ALWAYS_ASSERT(table >= 0 && table < numTables());
    auto* pcolumn = getColumn(table, column);
    ALWAYS_ASSERT(pcolumn);
    return pcolumn->get<T>(index, tupleidx);
  }

  /// set cell data
  template<class T>
  void set(sint table, String const& column, sint row, T const& value)
  {
    ALWAYS_ASSERT(table >= 0 && table < numTables());
    auto* pcolumn = getColumn(table, column);
    ALWAYS_ASSERT(pcolumn);
    pcolumn->set<T>(getIndex(table, row), value);
  }

  /// set cell data
  template<class T>
  void set(sint table, String const& column, sint row, T const& value, sint tupleidx)
  {
    ALWAYS_ASSERT(table >= 0 && table < numTables());
    auto* pcolumn = getColumn(table, column);
    ALWAYS_ASSERT(pcolumn);
    pcolumn->set<T>(getIndex(table, row), value, tupleidx);
  }

  /// set cell data
  template<class T>
  void set(sint table, String const& column, CellIndex index, T const& value)
  {
    ALWAYS_ASSERT(table >= 0 && table < numTables());
    auto* pcolumn = getColumn(table, column);
    ALWAYS_ASSERT(pcolumn);
    pcolumn->set<T>(index, value);
  }

  /// set cell data
  template<class T>
  void set(sint table, String const& column, CellIndex index, T const& value, sint tupleidx)
  {
    ALWAYS_ASSERT(table >= 0 && table < numTables());
    auto* pcolumn = getColumn(table, column);
    ALWAYS_ASSERT(pcolumn);
    pcolumn->set<T>(index, value, tupleidx);
  }

  /// append one row for each column of specified table
  /// return the cell index corresponding to this newly added row
  CellIndex addRow(sint table)
  {
    auto* ptab = getTable(table);
    ALWAYS_ASSERT(ptab);
    return ptab->addRow();
  }

  /// append n rows for each column of specified table
  /// the indices of these new rows are guaranteed to be continuous
  /// return the first cell index corresponding to these newly added rows
  CellIndex addRows(sint table, size_t n)
  {
    auto* ptab = getTable(table);
    ALWAYS_ASSERT(ptab);
    return ptab->addRows(n);
  }

  /// remove one row from specified table
  void removeRow(sint table, sint row)
  {
    auto* ptab = getTable(table);
    ALWAYS_ASSERT(ptab);
    return ptab->removeRow(row);
  }

  /// remove n continuous rows from specified table, till the end
  /// return number of rows really got deleted
  size_t removeRows(sint table, sint row, size_t n)
  {
    auto* ptab = getTable(table);
    ALWAYS_ASSERT(ptab);
    return ptab->removeRows(row, n);
  }

  /// get internal index of row number
  CellIndex getIndex(sint table, sint row) const
  {
    auto* ptab = getTable(table);
    ALWAYS_ASSERT(ptab);
    return ptab->getIndex(row);
  }

  /// get row number from internal index
  sint getRow(sint table, CellIndex index) const
  {
    auto* ptab = getTable(table);
    ALWAYS_ASSERT(ptab);
    return ptab->getRow(index);
  }

  size_t numRows(sint table) const
  {
    auto* ptab = getTable(table);
    ALWAYS_ASSERT(ptab);
    return ptab->numRows();
  }

  size_t numIndices(sint table) const
  {
    auto* ptab = getTable(table);
    ALWAYS_ASSERT(ptab);
    return ptab->numIndices();
  }
  // }}}
};

// }}} Data Collection

END_JOYFLOW_NAMESPACE
