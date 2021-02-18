#pragma once
#include "datatable_detail.h"

BEGIN_JOYFLOW_NAMESPACE

namespace detail {

// Numeric Interface {{{

template<class T>
class NumericDataColumnImpl
    : public DataColumn
    , public ObjectTracker<NumericDataColumnImpl<T>>
{
private:
  template <class U>
  DataColumnDesc makeNumericDesc(sint tupleSize, U const* defaultValue)
  {
    Vector<byte> defVal(sizeof(U));
    if (defaultValue)
      memcpy(defVal.data(), defaultValue, sizeof(U));
    return DataColumnDesc{
      TypeInfo<U>::dataType,
      TypeInfo<U>::tupleSize,
      sizeof(U),
      true,
      true,
      false,
      nullptr,
      defVal
    };
  }
  
public:
  NumericDataColumnImpl(String const& name, sint tupleSize, T const* defaultValue)
    : DataColumn(name, makeNumericDesc<T>(tupleSize, defaultValue))
    , storage_(new SharedVector<T>())
    , numericInterface_(this, TypeInfo<T>::dataType, tupleSize)
    , numericCompare_(this)
    , numericCopy_(this)
  {
  }
  NumericDataColumnImpl(String const& name, DataColumnDesc const& desc)
      : DataColumn(name, desc)
      , storage_(new SharedVector<T>())
      , numericInterface_(this, TypeInfo<T>::dataType, desc.tupleSize)
      , numericCompare_(this)
      , numericCopy_(this)
  {
    RUNTIME_CHECK(desc.tupleSize < MAX_TUPLE_SIZE,
        "tupleSize({}) >= MAX_TUPLE_SIZE({})", desc.tupleSize, MAX_TUPLE_SIZE);
    if (desc.defaultValue.size()>0) {
      ALWAYS_ASSERT(desc.defaultValue.size() == sizeof(T) * desc.tupleSize);
      memcpy(&defaultValue_, desc.defaultValue.data(), desc.defaultValue.size());
    }
  }
  OVERRIDE_NEW_DELETE;

public:
  class NumericInterfaceImpl : public NumericDataInterface
  {
  private:
    NumericDataColumnImpl* column_;

  public:
    NumericInterfaceImpl(NumericDataColumnImpl* column, DataType nativeType, sint tupleSize)
        : column_(column)
    {}
    virtual DataType dataType() const override { DEBUG_ASSERT(column_); return column_->dataType(); }
    virtual sint tupleSize() const override { DEBUG_ASSERT(column_); return column_->tupleSize(); }

    void const* getRawBufferRO(size_t offset, size_t count, DataType type) const override
    {
      size_t const endoffset = column_->length()*tupleSize();
      size_t const oldlength = column_->storage_->size();

      if (type!=dataType())
        return nullptr;
      if (offset+count > endoffset)
        return nullptr;
      if (oldlength < endoffset)
        return nullptr;

      T* arr = &(*column_->storage_)[0];
      return arr+offset;
    }

    void* getRawBufferRW(size_t offset, size_t count, DataType type) override
    {
      size_t const endoffset = column_->length()*tupleSize();
      size_t const oldlength = column_->storage_->size();

      if (type!=dataType())
        return nullptr;
      if (offset+count > endoffset)
        return nullptr;
      ASSERT(column_->isUnique());

      T* arr = &(*column_->storage_)[0];
      if (oldlength < endoffset) {
        auto const ts  = tupleSize();
        auto const *dv = column_->defaultValue_;
        column_->storage_->resize(endoffset);
        arr = &(*column_->storage_)[0];
        for (size_t i=oldlength; i<endoffset; ++i)
          arr[i] = dv[i%ts];
      }
      return arr+offset;
    }

    virtual bool getInt32Array(int32_t*  arrayToFill,
                               size_t&   outLength,
                               size_t    storageOffset,
                               size_t    count) const override
    {
      return column_->mapArray<int32_t>(arrayToFill, outLength, storageOffset, count);
    }
    virtual bool getInt64Array(int64_t*  arrayToFill,
                               size_t&   outLength,
                               size_t    storageOffset,
                               size_t    count) const override
    {
      return column_->mapArray<int64_t>(arrayToFill, outLength, storageOffset, count);
    }
    virtual bool getFloatArray(float*    arrayToFill,
                               size_t&   outLength,
                               size_t    storageOffset,
                               size_t    count) const override
    {
      return column_->mapArray<float>(arrayToFill, outLength, storageOffset, count);
    }
    virtual bool getDoubleArray(double*   arrayToFill,
                                size_t&   outLength,
                                size_t    storageOffset,
                                size_t    count) const override
    {
      return column_->mapArray<double>(arrayToFill, outLength, storageOffset, count);
    }


    virtual void setInt32Array(int32_t const* array, size_t storageOffset, size_t length) override
    {
      column_->unmapArray<int32_t>(array, storageOffset, length);
    }
    virtual void setInt64Array(int64_t const* array, size_t storageOffset, size_t length) override
    {
      column_->unmapArray<int64_t>(array, storageOffset, length);
    }
    virtual void setFloatArray(float const* array, size_t storageOffset, size_t length) override
    {
      column_->unmapArray<float>(array, storageOffset, length);
    }
    virtual void setDoubleArray(double const* array, size_t storageOffset, size_t length) override
    {
      column_->unmapArray<double>(array, storageOffset, length);
    }
  };

  class NumericCompareInterfaceImpl : public CompareInterface
  {
    NumericDataColumnImpl* self_;
    
    // primitive
    int cmp(typename TypeInfo<T>::StorageType const& a, typename TypeInfo<T>::StorageType const& b) const
    {
      if (a>b)
        return 1;
      else if (a<b)
        return -1;
      else
        return 0;
    }

    // vector
    template <int I>
    int cmp(glm::vec<I, T> const& a, glm::vec<I, T> const& b) const
    {
      int c = cmp(a[0], b[0]);
      if (c != 0)
        return c;
      if constexpr (I>1) {
        c = cmp(a[1], b[1]);
        if (c != 0)
          return c;
      }
      if constexpr (I>2) {
        c = cmp(a[2], b[2]);
        if (c != 0)
          return c;
      }
      if constexpr (I>3) {
        c = cmp(a[3], b[3]);
        if (c != 0)
          return c;
      }
      if constexpr (I>4) {
        for (int i=4;i<I;++i) {
          c = cmp(a[i], b[i]);
          if (c != 0)
            return c;
        }
      }
      return 0;
    }
  public:
    NumericCompareInterfaceImpl(NumericDataColumnImpl* self):
      self_(self)
    { }

    bool comparable(DataColumn const* that) const override
    {
      return self_->desc().dataType == that->desc().dataType &&
             self_->desc().elemSize == that->desc().elemSize &&
             self_->desc().tupleSize == that->desc().tupleSize;
    }

    int compare(CellIndex a, CellIndex b) const override
    {
      return cmp(self_->storage_->at(a.value()), self_->storage_->at(b.value()));
    }

    int compare(CellIndex a, DataColumn const* that, CellIndex b) const override
    {
      DEBUG_ASSERT(comparable(that));
      NumericDataColumnImpl<T> const* numericThat = static_cast<NumericDataColumnImpl<T> const*>(that);
      return cmp(self_->storage_->at(a.value()), numericThat->storage_->at(b.value()));
    }

    bool searchable(DataType dt, sint tupleSize, size_t size) const override
    {
      ASSERT(size == tupleSize * dataTypeSize(dt));
      return dt == self_->dataType() && tupleSize == self_->tupleSize();
    }

    CellIndex search(DataTable const* habitat, DataType dt, void const* data, size_t size) const override
    {
      DEBUG_ASSERT(dt == TypeInfo<T>::dataType);
      DEBUG_ASSERT(searchable(dt, size / dataTypeSize(dt), size));
      T const& val = *static_cast<T const*>(data);
      auto const& storage = *self_->storage_;
      for (size_t i=0, n=storage.size(); i<n; ++i) {
        if (val == storage[i] && habitat->getRow(CellIndex(i))!=-1)
          return CellIndex(i);
      }
      return CellIndex(-1);
    }

    size_t searchAll(Vector<CellIndex>& outMatches, DataTable const* habitat, DataType dt, void const* data, size_t size) const override
    {
      DEBUG_ASSERT(dt == TypeInfo<T>::dataType);
      DEBUG_ASSERT(searchable(dt, size / dataTypeSize(dt), size));
      outMatches.clear();
      size_t cnt = 0;
      T const& val = *static_cast<T const*>(data);
      auto const& storage = *self_->storage_;
      for (size_t i=0, n=storage.size(); i<n; ++i) {
        if (val == storage[i] && habitat->getRow(CellIndex(i))!=-1) {
          outMatches.emplace_back(i);
          ++cnt;
        }
      }
      return cnt;
    }
  };

  class NumericCopyImpl : public CopyInterface
  {
    NumericDataColumnImpl* self_;
  public:
    NumericCopyImpl(NumericDataColumnImpl* self): self_(self)
    { }

    bool copyable(DataColumn const* that) const override
    {
      return that->asNumericData() != nullptr;
    }

    bool copy(CellIndex a, CellIndex b) override
    {
      try {
        self_->storage_->at(a.value()) = self_->storage_->at(b.value());
        return true;
      } catch(AssertionFailure const&) {
        return false;
      } catch(CheckFailure const&) {
        return false;
      }
    }

    bool copy(CellIndex a, DataColumn const* that, CellIndex b) override
    {
      try {
        T val = that->asNumericData()->get<T>(b);
        self_->storage_->at(a.value()) = val;
        return true;
      } catch(AssertionFailure const&) {
        return false;
      } catch(CheckFailure const&) {
        return false;
      }
    }
  };

  NumericDataInterface*    asNumericData() override { return &numericInterface_; }
  FixSizedDataInterface* asFixSizedData() override { return nullptr; }
  BlobDataInterface*       asBlobData() override { return nullptr; }
  StringDataInterface*     asStringData() override { return nullptr; }
  CompareInterface const*  compareInterface() const override { return &numericCompare_; }
  CopyInterface*           copyInterface() override { return &numericCopy_; }

  template<class U>
  inline bool mapArray(U* arrayToFill, size_t& outLength, size_t storageOffset, size_t count) const
  {
    RUNTIME_CHECK(storageOffset!=-1, "Got invalid storageOffset");
    outLength = 0;
    auto const*  defaultValue = defaultValue_;
    size_t const storageSize  = storage_->size();
    if (count == -1)
      count = storageSize - storageOffset;
    size_t const destIndex = storageOffset + count;
    // fast pass
    if (destIndex<=storageSize) {
      if constexpr (std::is_same<T,U>::value) {
        T const* arr = &(*storage_)[0];
        switch (count) {
        case 4:
          arrayToFill[3] = arr[storageOffset + 3];
        case 3:
          arrayToFill[2] = arr[storageOffset + 2];
        case 2:
          arrayToFill[1] = arr[storageOffset + 1];
        case 1:
          arrayToFill[0] = arr[storageOffset];
          break;
        default:
          memcpy(arrayToFill, &(*storage_)[storageOffset], count * sizeof(T));
          break;
        }
      } else {
        auto const& storage = *storage_;
        for (size_t i = 0; i < count; ++i) {
          arrayToFill[i] = static_cast<U>(storage[storageOffset + i]);
        }
      }
      outLength = count;
    } else {
      if (storageOffset < storageSize) {
        size_t const copyCount = std::min(count, storageSize - storageOffset);
        if constexpr (std::is_same<T, U>::value) {
          outLength = copyCount;
          memcpy(arrayToFill, &(*storage_)[storageOffset], copyCount * sizeof(T));
        } else {
          auto const& storage = *storage_;
          for (size_t i = 0; i < copyCount; ++i) {
            arrayToFill[i] = static_cast<U>(storage[storageOffset + i]);
          }
          outLength = copyCount;
        }
        storageOffset += copyCount;
      }
      // fill rest of the array with defaultValue
      for (sint const ts = tupleSize(); storageOffset < destIndex; ++storageOffset) {
        arrayToFill[outLength++] = static_cast<U>(defaultValue[storageOffset % ts]);
      }
    }
    return true;
  }

  template<class U>
  void unmapArray(U const* array, size_t storageOffset, size_t length)
  {
    RUNTIME_CHECK(isUnique(),
        "Trying to modify shared column \"{}\", refcnt = {}", name_, storage_->refcnt());
    ALWAYS_ASSERT(storageOffset!=-1);
    ALWAYS_ASSERT(storageOffset + length <= length_ * desc_.tupleSize);
    auto&       storage      = *storage_;
    auto const* defaultValue = defaultValue_;
    size_t      storageSize  = storage.size();
    if (storageOffset + length > storageSize) {
      // TODO: gracefully grow this storage
      // storage.resize(storageOffset + length);

      // for now, let's just fill the whole array
      storage.resize(length_ * desc_.tupleSize);
      for (size_t cursor = storageSize, ts = tupleSize();
           cursor < storage.size();
           ++cursor) {
        storage[cursor] = defaultValue[cursor % ts];
      }
    }
    if constexpr (std::is_same<T, U>::value) {
      T* arr = &(*storage_)[0];
      switch (length) {
      case 4:
        arr[storageOffset + 3] = array[3];
      case 3:
        arr[storageOffset + 2] = array[2];
      case 2:
        arr[storageOffset + 1] = array[1];
      case 1:
        arr[storageOffset] = array[0];
        break;
      default:
        memcpy(&(*storage_)[storageOffset], array, length * sizeof(T));
        break;
      }
    } else {
      for (size_t i = 0; i < length; ++i) {
        storage[storageOffset + i] = static_cast<T>(array[i]);
      }
    }
  }

  String toString(CellIndex index, sint lengthLimit) const override
  {
    T val[MAX_TUPLE_SIZE];
    size_t osize=0;
    (void)lengthLimit;
    mapArray(val, osize, index.value()*tupleSize(), tupleSize());
    return tupleSize()>1
      ? fmt::format("({})", fmt::join(val, val+osize, ", "))
      : std::to_string(val[0]);
  }

  size_t length() const override { return length_; }

  /// number of elements really gets allocated
  size_t solidLength() const { return storage_->size() / desc_.tupleSize; }

  void reserve(size_t length) override
  {
    PROFILER_SCOPE_DEFAULT();
    RUNTIME_CHECK(isUnique(), "Trying to modify shared column \"{}\", refcnt = {}", name_, storage_->refcnt());
    if (length == length_ + 1) // special case for adding one element each time - in this situration we grow exponential
      storage_->push_back(defaultValue_[length % desc_.tupleSize]);
    else {
      storage_->reserve(length);
      resize(length); // fill all
    }
    length_ = length;
  }

  void resize(size_t length)
  {
    RUNTIME_CHECK(isUnique(), "Trying to modify shared column \"{}\", refcnt = {}", name_, storage_->refcnt());
    length_ = length;
    // little trick
    // TODO: do this seriously
    unmapArray<T>(nullptr, length*tupleSize(), 0);
  }

  DataColumnPtr clone() const override
  {
    auto nd = share();
    nd->makeUnique();
    return nd;
  }
  DataColumnPtr share() const override { return new NumericDataColumnImpl(*this); }
  void          makeUnique() override
  {
    if (isUnique())
      return;
    PROFILER_SCOPE_DEFAULT();
    auto storage = storage_;
    storage_     = new SharedVector<T>(*storage);
  }
  bool isUnique() const override { return storage_ && storage_->refcnt() == 1; }

  size_t shareCount() const override { return storage_ ? storage_->refcnt() : 0; }

  void defragment(DefragmentInfo const& how) override
  {
    for (auto const& op : how.operations()) {
      switch (op.op) {
      case DefragmentInfo::OpCode::MOVE:
        std::memmove(&(*storage_)[op.args[1] * desc_.tupleSize],
                     &(*storage_)[op.args[0] * desc_.tupleSize],
                     sizeof(T) * desc_.tupleSize);
        break;
      default:
        break;
      }
    }
    if (storage_->capacity() > how.finalSize()*tupleSize()) {
      storage_->resize(how.finalSize()*tupleSize());
      storage_->shrink_to_fit();
      length_ = how.finalSize();
    }
  }

  static void convertAndCopyContent(DataColumn* dst, size_t dstStartOffset, DataColumn const* src, size_t srcStartOffset, size_t elemCount)
  {
    PROFILER_SCOPE_DEFAULT();
    DEBUG_ASSERT(src->asNumericData());
    DEBUG_ASSERT(dst->asNumericData());
    ALWAYS_ASSERT(dstStartOffset + elemCount <= dst->length() * dst->tupleSize());
    ALWAYS_ASSERT(srcStartOffset + elemCount <= src->length() * src->tupleSize());
    size_t  fillLen = 0;
    sint const dstTS = dst->tupleSize();
    sint const srcTS = src->tupleSize();

    static_cast<NumericDataColumnImpl<T>*>(dst)->resize(dstStartOffset + elemCount * dstTS);
    if (src->dataType()==dst->dataType() && srcTS == dstTS) { // fast copy
      auto const cpcnt = elemCount*srcTS;
      auto dt = src->dataType();
      switch (dt) {
      case DataType::INT32:
        src->asNumericData()->getInt32Array(
            (int32_t*)dst->asNumericData()->getRawBufferRW(dstStartOffset, cpcnt, dt),
            fillLen, srcStartOffset, cpcnt);
        break;
      case DataType::UINT32:
        src->asNumericData()->getUint32Array(
            (uint32_t*)dst->asNumericData()->getRawBufferRW(dstStartOffset, cpcnt, dt),
            fillLen, srcStartOffset, cpcnt);
        break;
      case DataType::INT64:
        src->asNumericData()->getInt64Array(
            (int64_t*)dst->asNumericData()->getRawBufferRW(dstStartOffset, cpcnt, dt),
            fillLen, srcStartOffset, cpcnt);
        break;
      case DataType::UINT64:
        src->asNumericData()->getUint64Array(
            (uint64_t*)dst->asNumericData()->getRawBufferRW(dstStartOffset, cpcnt, dt),
            fillLen, srcStartOffset, cpcnt);
        break;
      case DataType::FLOAT:
        src->asNumericData()->getFloatArray(
            (float*)dst->asNumericData()->getRawBufferRW(dstStartOffset, cpcnt, dt),
            fillLen, srcStartOffset, cpcnt);
        break;
      case DataType::DOUBLE:
        src->asNumericData()->getDoubleArray(
            (double*)dst->asNumericData()->getRawBufferRW(dstStartOffset, cpcnt, dt),
            fillLen, srcStartOffset, cpcnt);
        break;
      default:
        throw TypeError(fmt::format("got unconvertable format when joining column \"{}\"", src->name()));
      }
    } else { // need conversion
      switch (dst->dataType()) {
      case DataType::INT32:
      case DataType::UINT32: {
        int32_t tmpval[MAX_TUPLE_SIZE];
        for (size_t ridx=srcStartOffset, widx=dstStartOffset;
             ridx < srcStartOffset+elemCount*srcTS;
             ridx += srcTS, widx += dstTS) {
          src->asNumericData()->getInt32Array(tmpval, fillLen, ridx, srcTS);
          dst->asNumericData()->setInt32Array(tmpval, widx, fillLen);
        }
        break;
      }
      case DataType::INT64:
      case DataType::UINT64: {
        int64_t tmpval[MAX_TUPLE_SIZE];
        for (size_t ridx=srcStartOffset, widx=dstStartOffset;
             ridx < srcStartOffset+elemCount*srcTS;
             ridx += srcTS, widx += dstTS) {
          src->asNumericData()->getInt64Array(tmpval, fillLen, ridx, srcTS);
          dst->asNumericData()->setInt64Array(tmpval, widx, fillLen);
        }
        break;
      }
      case DataType::FLOAT: {
        float tmpval[MAX_TUPLE_SIZE];
        for (size_t ridx=srcStartOffset, widx=dstStartOffset;
             ridx < srcStartOffset+elemCount*srcTS;
             ridx += srcTS, widx += dstTS) {
          src->asNumericData()->getFloatArray(tmpval, fillLen, ridx, srcTS);
          dst->asNumericData()->setFloatArray(tmpval, widx, fillLen);
        }
        break;
      }
      case DataType::DOUBLE: {
        double tmpval[MAX_TUPLE_SIZE];
        for (size_t ridx=srcStartOffset, widx=dstStartOffset;
             ridx < srcStartOffset+elemCount*srcTS;
             ridx += srcTS, widx += dstTS) {
          src->asNumericData()->getDoubleArray(tmpval, fillLen, ridx, srcTS);
          dst->asNumericData()->setDoubleArray(tmpval, widx, fillLen);
        }
        break;
      }
      default:
        throw TypeError(fmt::format("got unconvertable format when joining column \"{}\"", src->name()));
      }
    }
  }

  DataColumn* join(DataColumn const* their) override
  {
    DataColumn* wd = this; // working data
    if (their) {
      size_t const oldlength = length();
      size_t const newlength = oldlength + their->length();

      auto* numinterface = their->asNumericData();
      if (!numinterface) {
        reserve(newlength);
        return this;
      }
      DataType   targetDataType  = dataType();
      sint const targetTupleSize = std::max(tupleSize(), their->tupleSize());

      if (dataType() != their->dataType()) {
        DataType constexpr BLOB      = DataType::BLOB;
        DataType constexpr DOUBLE    = DataType::DOUBLE;
        DataType constexpr FLOAT     = DataType::FLOAT;
        DataType constexpr INT32     = DataType::INT32;
        DataType constexpr INT64     = DataType::INT64;
        DataType constexpr STRUCTURE = DataType::STRUCTURE;
        DataType constexpr UINT32    = DataType::UINT32;
        DataType constexpr UINT64    = DataType::UINT64;
        DataType constexpr UNKNOWN   = DataType::UNKNOWN;
        sint     constexpr COUNT     = static_cast<sint>(DataType::COUNT);

        // clang-format off
        static DataType convertionDic[COUNT][COUNT] = {
          // vertical   -> our type
          // horizontal -> their type
          // value      -> destiny type when performing join operation

          //      //    INT32    UINT32    INT64   UINT64    FLOAT   DOUBLE   STRUCT     BLOB
          /* INT32*/    INT32,    INT32,   INT64,   INT64,   INT32,   INT64, UNKNOWN, UNKNOWN,
          /*UINT32*/   UINT32,   UINT32,  UINT64,  UINT64,   INT32,   INT64, UNKNOWN, UNKNOWN,
          /* INT64*/    INT64,    INT64,   INT64,   INT64,   INT64,   INT64, UNKNOWN, UNKNOWN,
          /*UINT64*/   UINT64,   UINT64,  UINT64,  UINT64,   INT64,   INT64, UNKNOWN, UNKNOWN,
          /* FLOAT*/    FLOAT,    FLOAT,  DOUBLE,  DOUBLE,   FLOAT,  DOUBLE, UNKNOWN, UNKNOWN,
          /*DOUBLE*/   DOUBLE,   DOUBLE,  DOUBLE,  DOUBLE,  DOUBLE,  DOUBLE, UNKNOWN, UNKNOWN,
          /*STRUCT*/   UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN,
          /*  BLOB*/   UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN,
        };
        // clang-format on

        ALWAYS_ASSERT(sint(dataType()) < COUNT);
        ALWAYS_ASSERT(sint(their->dataType()) < COUNT);
        DataType destType = convertionDic[sint(dataType())][sint(their->dataType())];

        if (destType == UNKNOWN) {
          reserve(newlength);
          return this;
        }
        targetDataType = destType;
      }

      if (dataType() != targetDataType || targetTupleSize != tupleSize()) {
        // convert our own data type
        switch (targetDataType) {
        case DataType::INT32:
        case DataType::UINT32: {
          int32_t defaultIntValue[MAX_TUPLE_SIZE] = {0};
          for (sint i = 0; i < tupleSize(); ++i)
            defaultIntValue[i] = static_cast<int32_t>(defaultValue_[i]);
          wd = new NumericDataColumnImpl<int32_t>(name(), targetTupleSize, defaultIntValue);
          break;
        }
        case DataType::INT64:
        case DataType::UINT64: {
          int64_t defaultIntValue[MAX_TUPLE_SIZE] = {0};
          for (sint i = 0; i < tupleSize(); ++i)
            defaultIntValue[i] = static_cast<int64_t>(defaultValue_[i]);
          wd = new NumericDataColumnImpl<int64_t>(name(), targetTupleSize, defaultIntValue);
          break;
        }
        case DataType::FLOAT: {
          float defaultFloatValue[MAX_TUPLE_SIZE] = {0};
          for (sint i = 0; i < tupleSize(); ++i)
            defaultFloatValue[i] = static_cast<float>(defaultValue_[i]);
          wd = new NumericDataColumnImpl<float>(name(), targetTupleSize, defaultFloatValue);
          break;
        }
        case DataType::DOUBLE: {
          double defaultDoubleValue[MAX_TUPLE_SIZE] = {0};
          for (sint i = 0; i < tupleSize(); ++i)
            defaultDoubleValue[i] = static_cast<double>(defaultValue_[i]);
          wd = new NumericDataColumnImpl<double>(name(), targetTupleSize, defaultDoubleValue);
          break;
        }
        default:
          throw TypeError("got unconvertable format at column join");
        }
        wd->reserve(newlength);
        convertAndCopyContent(wd, 0, this, 0, length());
      } else {
        reserve(newlength);
        wd = this;
      }

      convertAndCopyContent(wd, oldlength*wd->tupleSize(), their, 0, their->length());
    }
    return wd;
  }

  void move(CellIndex dst, CellIndex src, size_t count) override
  {
    RUNTIME_CHECK(isUnique(), "Trying to modify shared column \"{}\", refcnt = {}", name_, storage_->refcnt());
    size_t const srcStartOffset = src.value() * tupleSize();
    size_t const srcEndOffset   = (src.value() + count) * tupleSize();
    size_t const dstStartOffset = dst.value() * tupleSize();
    size_t const dstEndOffset   = (dst.value() + count) * tupleSize();

    // nothing to do
    if (srcStartOffset == dstStartOffset)
      return;

    // both out of storage bound
    // means they are all default-valued
    // we can left it as-is
    if (srcStartOffset >= storage_->size() && dstStartOffset >= storage_->size())
      return;

    resize(std::max(srcEndOffset, dstEndOffset));
    std::memmove(&storage_->at(dstStartOffset), &storage_->at(srcStartOffset), sizeof(T)*tupleSize()*count);

    // fill up moved content with default value
    if (srcStartOffset < dstStartOffset) {
      size_t const srcCopyEnd = std::min(srcEndOffset, dstStartOffset);
      for (size_t i=srcStartOffset; i<srcCopyEnd; ++i)
        storage_->at(i) = defaultValue_[i%tupleSize()];
    } else if (srcStartOffset > dstStartOffset) {
      size_t const srcCopyStart = std::max(srcStartOffset, dstEndOffset);
      for (size_t i=srcCopyStart; i<srcEndOffset; ++i)
        storage_->at(i) = defaultValue_[i%tupleSize()];
    }
  }

  void countMemory(size_t& sharedBytes, size_t& unsharedBytes) const override
  {
    unsharedBytes = sizeof(*this);
    sharedBytes = 0;

    size_t datasize = storage_->capacity() * sizeof(T);
    if (storage_->refcnt() == 1)
      unsharedBytes += datasize;
    else
      sharedBytes += datasize;
  }

protected:
  NumericDataColumnImpl(NumericDataColumnImpl const& that)
      : DataColumn(that.name(), that.desc())
      , storage_(that.storage_)
      , length_(that.length_)
      , numericInterface_(this, that.dataType(), that.tupleSize())
      , numericCompare_(this)
      , numericCopy_(this)
  {
    memcpy(defaultValue_, that.defaultValue_, sizeof(defaultValue_));
  }
  friend class NumericInterfaceImpl;
  friend class NumericCompareInterfaceImpl;
  friend class NumericCopyImpl;

  T                             defaultValue_[MAX_TUPLE_SIZE] = {0};
  IntrusivePtr<SharedVector<T>> storage_;
  size_t                        length_;
  NumericInterfaceImpl          numericInterface_;
  NumericCompareInterfaceImpl   numericCompare_;
  NumericCopyImpl               numericCopy_;
};

// }}} Numeric Interface

}

END_JOYFLOW_NAMESPACE

