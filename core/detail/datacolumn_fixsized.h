#pragma once
#include "datatable_detail.h"

BEGIN_JOYFLOW_NAMESPACE

namespace detail {

// Structured Data Interface {{{

class StructuredDataColumnImpl
    : public DataColumn
    , public CopyInterface
    , public ObjectTracker<StructuredDataColumnImpl>
{
  friend class StructuredInterfaceImpl;

public:
  class StructuredInterfaceImpl : public FixSizedDataInterface
  {
  private:
    friend class StructuredDataColumnImpl;
    StructuredDataColumnImpl* column_;
    size_t const              itemSize_;
    StructuredInterfaceImpl(StructuredDataColumnImpl* column, size_t itemSize)
        : column_(column), itemSize_(itemSize)
    {}

  public:
    virtual size_t itemSize() const override { return itemSize_; }

    virtual bool   getItems(void*     itemArray,
                                 size_t&   outLength,
                                 CellIndex itemIndex,
                                 size_t    count) const override
    {
      outLength = 0;
      if (!column_) {
        return false;
      }
      if (!itemIndex.valid()) {
        return false;
      }
      auto const*  defaultValue  = column_->defaultValue_.data();
      size_t const containerSize = column_->objects_->size();
      if (itemIndex < containerSize / itemSize_) {
        size_t const copyCount = std::min(count, containerSize / itemSize_ - itemIndex.value());
        outLength              = copyCount;
        memcpy(itemArray,
               &(*(column_->objects_))[itemIndex.value() * itemSize_],
               copyCount * itemSize_);
      }
      // fill the rest with default value
      for (; outLength < count; ++outLength) {
        memcpy(static_cast<byte*>(itemArray) + outLength * itemSize_, defaultValue, itemSize_);
      }
      outLength = count;
      return true;
    }

    virtual bool setItems(void const* itemArray, CellIndex itemIndex, size_t count) override
    {
      RUNTIME_CHECK(column_->isUnique(), "Trying to modify shared column \"{}\", refcnt = {}", column_->name(), column_->objects_->refcnt());
      if (!column_) {
        return false;
      }
      if (!itemIndex.valid())
        return false;
      auto const* defaultValue  = column_->defaultValue_.data();
      auto&       objects       = *column_->objects_;
      size_t      containerSize = objects.size();
      if (containerSize <= (itemIndex.value() + count) * itemSize_) {
        objects.resize((itemIndex.value() + count) * itemSize_);
        for (size_t i = containerSize, destIndex = itemIndex.value() * itemSize_; i < destIndex;
             i += itemSize_) {
          memcpy(&objects[i], defaultValue, itemSize_);
        }
      }
      memcpy(&objects[itemIndex.value() * itemSize_], itemArray, itemSize_ * count);
      return true;
    }

    virtual void setToStringMethod(String(*toString)(void const*)) override
    {
      column_->toStringMethod = toString;
    }
  };
  
  // copy interface {{{
  bool copyable(DataColumn const* that) const override
  {
    return that && that->asFixSizedData() && desc().compatible(that->desc());
  }

  bool copy(CellIndex a, CellIndex b) override
  {
    auto& objs = *objects_;
    if (auto sz = objs.size(); a >= sz || b >= sz)
      return false;
    objs[a.value()] = objs[b.value()];
    return true;
  }

  bool copy(CellIndex a, DataColumn const* that, CellIndex b) override
  {
    DEBUG_ASSERT(copyable(that));
    auto& objs = *objects_;
    if (a >= objs.size())
      return false;
    size_t outsz = 0;
    return that->asFixSizedData()->getItems(&objs[a.value()], outsz, b, 1) && outsz == desc_.elemSize;
  }
  // copy interface }}}

public:
  StructuredDataColumnImpl(String const& name, DataColumnDesc const& desc)
      : DataColumn(name, desc)
      , objects_(new SharedVector<byte>())
      , structuredInterface_(this, desc.elemSize)
      , defaultValue_(desc.elemSize)
  {
    if (desc.defaultValue.size() == desc.elemSize) {
      defaultValue_ = desc.defaultValue;
    }
  }
  OVERRIDE_NEW_DELETE;

  NumericDataInterface*    asNumericData() override { return nullptr; }
  FixSizedDataInterface*   asFixSizedData() override { return &structuredInterface_; }
  BlobDataInterface*       asBlobData() override { return nullptr; }
  StringDataInterface*     asStringData() override { return nullptr; }
  CopyInterface*           copyInterface() override { return this; }

  size_t length() const override { return objects_->size() / desc_.elemSize; }

  String toString(CellIndex index, sint lengthLimit) const override
  {
    if (toStringMethod)
      return toStringMethod(&objects_->at(index.value() * desc_.elemSize));
    return fmt::format("struct of {} bytes", desc_.elemSize);
  }

  void reserve(size_t length) override
  {
    // do nothing
    // we support out-of-boundry access
    // TODO: keep reserved length for bound checking?
    (void)length;
  }

  void resize(size_t length)
  {
    // TODO: do resize
  }

  DataColumnPtr clone() const override
  {
    auto cp = share();
    cp->makeUnique();
    return cp;
  }
  DataColumnPtr share() const override { return new StructuredDataColumnImpl(*this); }
  void          makeUnique() override
  {
    if (isUnique())
      return;
    PROFILER_SCOPE_DEFAULT();
    auto objs = objects_;
    objects_  = new SharedVector<byte>(*objs);
  }
  bool isUnique() const override { return objects_ && objects_->refcnt() == 1; }

  size_t shareCount() const override { return objects_ ? objects_->refcnt() : 0; }

  void defragment(DefragmentInfo const& how) override
  {
    auto const elemSize = desc_.elemSize;
    for (auto const& op : how.operations()) {
      switch (op.op) {
      case DefragmentInfo::OpCode::MOVE:
        std::memmove(
            &(*objects_)[op.args[1] * elemSize], &(*objects_)[op.args[0] * elemSize], elemSize);
        break;
      default:
        break;
      }
    }
    if (objects_->capacity() > how.finalSize() * elemSize) {
      objects_->resize(how.finalSize() * elemSize);
      objects_->shrink_to_fit();
    }
  }

  DataColumn* join(DataColumn const* their) override
  {
    RUNTIME_CHECK(isUnique(), "Trying to modify shared column \"{}\", refcnt = {}", name_, objects_->refcnt());
    size_t const oldlength = length();
    reserve(length() + their->length());
    if (their->asFixSizedData()) {
      StructuredDataColumnImpl const* sdc = static_cast<StructuredDataColumnImpl const*>(their);
      if (desc_.elemSize == sdc->desc_.elemSize) {
        // TODO: maybe check type?
        std::memcpy(&objects_->at(oldlength*desc_.elemSize), &sdc->objects_->at(0), their->length()*desc_.elemSize);
      }
    }
    return this;
  }

  // TODO: this whole function can be greatly optimized
  void move(CellIndex dst, CellIndex src, size_t count) override
  {
    RUNTIME_CHECK(isUnique(), "Trying to modify shared column \"{}\", refcnt = {}", name_, objects_->refcnt());
    size_t const oldlength      = length();
    size_t const srcStartOffset = src.value();
    size_t const srcEndOffset   = src.value() + count;
    size_t const dstStartOffset = dst.value();
    size_t const dstEndOffset   = dst.value() + count;
    size_t const elemSize       = desc_.elemSize;

    // TODO: check length doesn't exceed reserved length
    reserve(std::max(srcEndOffset, dstEndOffset));

    // check if what to copy is all default-valued
    // and where to copy to is also already default valued
    if (objects_->size() <= srcStartOffset * elemSize &&
        objects_->size() <= dstStartOffset * elemSize )
      return;

    size_t const oldobjlength = objects_->size();
    objects_->resize(dstEndOffset*elemSize);
    if (srcEndOffset * elemSize > oldobjlength) {
      for (size_t i = oldobjlength / elemSize; i < srcEndOffset; ++i) {
        std::memcpy(&objects_->at(i*elemSize), defaultValue_.data(), elemSize);
      }
    }
    
    if (srcEndOffset > dstStartOffset) {
      // when src's back overlaps dst's front, we'll need to move from back to front:
      // ---------SRC---------
      //               ---------DST---------
      for (size_t i=1; i<=count; ++i) {
        size_t const srcIdx = (srcEndOffset-i)*elemSize;
        size_t const dstIdx = (dstEndOffset-i)*elemSize;
        void const* srcptr = nullptr;
        if (srcIdx>=objects_->size())
          srcptr = defaultValue_.data();
        else
          srcptr = &objects_->at(srcIdx);

        void* dstptr = &objects_->at(dstIdx);
        std::memcpy(dstptr, srcptr, elemSize);
      }
    } else {
      //               ---------SRC---------
      // ---------DST---------
      size_t fillLen = 0;
      structuredInterface_.getItems(
          &objects_->at(dstStartOffset*elemSize),
          fillLen,
          CellIndex(srcStartOffset),
          count);
    }

    // fill up moved content with default value
    if (srcStartOffset < dstStartOffset) {
      size_t const srcCopyEnd = std::min(srcEndOffset, dstStartOffset);
      for (size_t i=srcStartOffset; i<srcCopyEnd; ++i) {
        size_t const offset = i * elemSize;
        if (offset < objects_->size())
          std::memcpy(&objects_->at(offset), defaultValue_.data(), elemSize);
      }
    } else if (srcStartOffset > dstStartOffset) {
      size_t const srcCopyStart = std::max(srcStartOffset, dstEndOffset);
      for (size_t i=srcCopyStart; i<srcEndOffset; ++i) {
        size_t const offset = i * elemSize;
        if (offset < objects_->size())
          std::memcpy(&objects_->at(offset), defaultValue_.data(), elemSize);
      }
    }
  }

  void countMemory(size_t& sharedBytes, size_t& unsharedBytes) const override
  {
    unsharedBytes = sizeof(*this);
    sharedBytes = 0;

    unsharedBytes += desc_.elemSize; // default value
    size_t datasize = objects_->size();

    if (objects_->refcnt() == 1)
      unsharedBytes += datasize;
    else
      sharedBytes += datasize;
  }

protected:
  StructuredDataColumnImpl(StructuredDataColumnImpl const& that)
      : DataColumn(that.name(), that.desc())
      , toStringMethod(that.toStringMethod)
      , objects_(that.objects_)
      , defaultValue_(that.desc().elemSize)
      , structuredInterface_(this, that.desc().elemSize)
  {
    if (!desc_.defaultValue.empty()) {
      ASSERT(desc_.defaultValue.size() == desc_.elemSize);
      memcpy(defaultValue_.data(), that.defaultValue_.data(), desc_.elemSize);
    }
  }
  // TODO: maybe a typeid or type string?
  
  String (*toStringMethod)(void const*) = nullptr;
  IntrusivePtr<SharedVector<byte>> objects_;
  Vector<byte>                     defaultValue_;
  StructuredInterfaceImpl          structuredInterface_;
};

// }}} Structured Data Interface

}

END_JOYFLOW_NAMESPACE
