#pragma once
#include "datatable_detail.h"

BEGIN_JOYFLOW_NAMESPACE

namespace detail {

// Container Interface {{{
class ContainerDataColumnImpl : public DataColumn, public CopyInterface
{
  friend class ContainerDataInterfaceImpl;

public:
  OVERRIDE_NEW_DELETE;

  class ContainerDataInterfaceImpl : public VectorDataInterface
  {
    ContainerDataColumnImpl* column_ = nullptr;

  public:
    ContainerDataInterfaceImpl(ContainerDataColumnImpl* column) : column_(column) {}

    DataType dataType() const override { return column_->dataType(); }
    sint     tupleSize() const override { return column_->tupleSize(); }

    void* data(CellIndex index) override
    {
      RUNTIME_CHECK(index.valid() && index < column_->lists_->size(), "invalid index {}, should in range [0,{})", index.value(), column_->lists_->size());
      return (*column_->lists_)[index.value()].data();
    }
    size_t size(CellIndex index) const override
    {
      RUNTIME_CHECK(index.valid() && index < column_->lists_->size(), "invalid index {}, should in range [0,{})", index.value(), column_->lists_->size());
      return (*column_->lists_)[index.value()].size();
    }
    size_t capacity(CellIndex index) const override
    {
      RUNTIME_CHECK(index.valid() && index < column_->lists_->size(), "invalid index {}, should in range [0,{})", index.value(), column_->lists_->size());
      return (*column_->lists_)[index.value()].capacity();
    }

  protected:
    Vector<byte>* rawVectorPtr(CellIndex index) override
    {
      RUNTIME_CHECK(index.valid() && index < column_->lists_->size(), "invalid index {}, should in range [0,{})", index.value(), column_->lists_->size());
      return &((*column_->lists_)[index.value()]);
    }
  };

  ContainerDataColumnImpl(String const& name, DataColumnDesc const& desc)
      : DataColumn(name, desc), interface_(this)
  {
    lists_ = new SharedVector<Vector<byte>>();
  }

  String toString(CellIndex index, sint lengthLimit) const override
  {
    RUNTIME_CHECK(index.valid() && index < lists_->size(), "invalid index {}, should in range [0,{})", index.value(), lists_->size());
    auto const& rawvec = lists_->at(index.value());
    if (rawvec.empty())
      return "[]";
    if (tupleSize() == 1) {
      switch(dataType()) {
      case joyflow::DataType::INT32:
        return fmt::format("[{}]", fmt::join(*interface_.asVector<int32_t>(index), ", "));
      case joyflow::DataType::UINT32:
        return fmt::format("[{}]", fmt::join(*interface_.asVector<uint32_t>(index), ", "));
      case joyflow::DataType::INT64:
        return fmt::format("[{}]", fmt::join(*interface_.asVector<int64_t>(index), ", "));
      case joyflow::DataType::UINT64:
        return fmt::format("[{}]", fmt::join(*interface_.asVector<uint64_t>(index), ", "));
      case joyflow::DataType::FLOAT:
        return fmt::format("[{}]", fmt::join(*interface_.asVector<float>(index), ", "));
      case joyflow::DataType::DOUBLE:
        return fmt::format("[{}]", fmt::join(*interface_.asVector<double>(index), ", "));
      default:
        ; // pass
      }
    }
    return fmt::format("vector<{}[{}]> of {} elements", dataTypeName(dataType()), tupleSize(), lists_->at(index.value()).size()/(dataTypeSize(dataType())*tupleSize()));
  }

  // copy interface {{{
  bool copyable(DataColumn const* that) const override
  {
    return that && that->asVectorData() &&
      that->dataType() == dataType() && that->tupleSize() == tupleSize();
  }

  bool copy(CellIndex a, CellIndex b) override
  {
    if (auto sz = lists_->size(); a >= sz || b >= sz)
      return false;
    lists_->at(a.value()) = lists_->at(b.value());
    return true;
  }

  bool copy(CellIndex a, DataColumn const* that, CellIndex b) override
  {
    DEBUG_ASSERT(copyable(that));
    if (a >= lists_->size())
      return false;
    if (auto* v = that->asVectorData()->rawVectorPtr(b)) {
      lists_->at(a.value()) = *v;
    }
    return false;
  }
  // copy interface }}}

  size_t length() const override { return lists_->size(); }
  void   reserve(size_t length) override { lists_->resize(length); }

  DataColumnPtr share() const override
  {
    auto* column   = new ContainerDataColumnImpl(name(), desc_);
    column->lists_ = lists_;
    return column;
  }
  DataColumnPtr clone() const override
  {
    auto cp = share();
    cp->makeUnique();
    return cp;
  }
  void makeUnique() override
  {
    if (isUnique())
      return;
    PROFILER_SCOPE_DEFAULT();
    auto lists = lists_;
    lists_     = new SharedVector<Vector<byte>>(*lists);
  }
  bool isUnique() const override { return lists_ && lists_->refcnt() == 1; }
  size_t shareCount() const override { return lists_ ? lists_->refcnt() : 0; }
  void defragment(DefragmentInfo const& how) override
  {
    // TODO: defragment for vector column
  }

  VectorDataInterface* asVectorData() override { return &interface_; }
  CopyInterface*       copyInterface() override { return this; }

  DataColumn* join(DataColumn const* their) override
  {
    size_t oldlength = length();
    reserve(length() + their->length());
    if (their->asVectorData() &&
        dataType() == their->dataType() &&
        tupleSize() == their->tupleSize()) {

      auto*       mvi = asVectorData();
      auto const* tvi = their->asVectorData();
      sint const  ts  = tupleSize();

#define _TUPLE_CASE(N,TP) \
      case N: \
        *mvi->asVector<TP>(widx) = *tvi->asVector<TP>(ridx); break

#define _TYPE_CASE(TCODE,TP) \
      case TCODE: {  \
        using _v2 = glm::vec<2,TP>; \
        using _v3 = glm::vec<3,TP>; \
        using _v4 = glm::vec<4,TP>; \
        using _v9 = glm::mat<3,3,TP>; \
        using _v16 = glm::mat<4,4,TP>; \
        switch(ts) { \
          _TUPLE_CASE(1, TP);\
          _TUPLE_CASE(2, _v2); \
          _TUPLE_CASE(3, _v3); \
          _TUPLE_CASE(4, _v4); \
          _TUPLE_CASE(9, _v9); \
          _TUPLE_CASE(16, _v16); \
          default: RUNTIME_CHECK(false, "Bad tuple size: {}", ts); \
        }\
      }

      for (CellIndex ridx(0), widx(oldlength); ridx<their->length(); ++ridx, ++widx) {
        switch(dataType()) {
          _TYPE_CASE(DataType::INT32,  int32_t)
          _TYPE_CASE(DataType::UINT32, uint32_t)
          _TYPE_CASE(DataType::INT64,  int64_t)
          _TYPE_CASE(DataType::UINT64, uint64_t)
          _TYPE_CASE(DataType::FLOAT,  float)
          _TYPE_CASE(DataType::DOUBLE, double)
          default:
            throw TypeError(fmt::format("Unsupported type {} for container column to join", dataType()));
        }
      }

#undef _TUPLE_CASE
#undef _TYPE_CASE
    }
    return this;
  }

  void move(CellIndex dst, CellIndex src, size_t count) override
  {
    RUNTIME_CHECK(isUnique(), "Trying to modify shared column \"{}\", refcnt = {}", name_, lists_->refcnt());
    size_t const oldlength      = length();
    size_t const srcStartOffset = src.value();
    size_t const srcEndOffset   = src.value() + count;
    size_t const dstStartOffset = dst.value();
    size_t const dstEndOffset   = dst.value() + count;

    // nothing to do
    if (srcStartOffset == dstStartOffset)
      return;

    reserve(std::max(srcEndOffset, dstEndOffset));

    if (srcEndOffset > dstStartOffset) {
      // when src's back overlaps dst's front, we'll need to move from back to front:
      // ---------SRC---------
      //               ---------DST---------
      for (size_t i=1; i<=count; ++i) {
        lists_->at(dstEndOffset-i)=std::move(lists_->at(srcEndOffset-i));
      }
    } else {
      //               ---------SRC---------
      // ---------DST---------
      for (size_t i=0; i<count; ++i) {
        lists_->at(dstStartOffset+i)=std::move(lists_->at(srcStartOffset+i));
      }
    }

    // fill up moved content with empty vector
    if (srcStartOffset < dstStartOffset) {
      size_t const srcCopyEnd = std::min(srcEndOffset, dstStartOffset);
      for (size_t i=srcStartOffset; i<srcCopyEnd; ++i)
        lists_->at(i) = Vector<byte>();
    } else if (srcStartOffset > dstStartOffset) {
      size_t const srcCopyStart = std::max(srcStartOffset, dstEndOffset);
      for (size_t i=srcCopyStart; i<srcEndOffset; ++i)
        lists_->at(i) = Vector<byte>();
    }
  }

  void countMemory(size_t& sharedBytes, size_t& unsharedBytes) const override
  {
    unsharedBytes = sizeof(*this);
    sharedBytes = 0;

    size_t datasize = 0;
    for (auto const& v: *lists_) {
      datasize += v.capacity();
    }
    if (lists_->refcnt()==1)
      unsharedBytes += datasize;
    else
      sharedBytes += datasize;
  }

protected:
  IntrusivePtr<SharedVector<Vector<byte>>> lists_;
  ContainerDataInterfaceImpl               interface_;
};
// }}}

}

END_JOYFLOW_NAMESPACE

