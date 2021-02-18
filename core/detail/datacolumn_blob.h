#pragma once
#include "datatable_detail.h"

BEGIN_JOYFLOW_NAMESPACE

namespace detail {

// Blob Interface {{{

/// blobs are represented as integer indices inside this storage
class BlobStorage
    : public ReferenceCounted<BlobStorage>
    , public ObjectTracker<BlobStorage>
{
public:
  BlobStorage()                        = default;
  BlobStorage(BlobStorage const& that) {
    blobs_ = that.blobs_;
  }
  ~BlobStorage() {}
  OVERRIDE_NEW_DELETE;

public:
  struct Key
  {
    size_t      hash;
    void const* data;
    size_t      size;
  };

  size_t addBlob(void const* data, size_t size)
  {
    size_t hash = xxhash(data, size);

    Key  k   = {hash, data, size};
    sint idx = blobs_.indexof(k);
    if (idx != -1) {
      intrusiveAddRef(blobs_[idx].get());
      return idx;
    }

    auto* blob = new SharedBlob(data, size, hash);
    k          = {hash, blob->data, blob->size};
    intrusiveAddRef(blob); // add one for the column holding this
    return blobs_.insert(k, blob);
  }

  size_t addBlob(SharedBlobPtr blob)
  {
    RUNTIME_CHECK(blob, "Invalid blob data");

    Key  k   = {blob->hash, blob->data, blob->size};
    sint idx = blobs_.indexof(k);
    if (idx != -1) {
      intrusiveAddRef(blobs_[idx].get());
      return idx;
    }
    intrusiveAddRef(blob.get()); // add one for the column holding this
    return blobs_.insert(k, blob);
  }

  size_t addBlob(size_t blobIndex)
  {
    intrusiveAddRef(blobs_[blobIndex].get());
    return blobIndex;
  }

  /// release blob's ref count by 1
  /// return true if the blob got deleted
  /// else return false
  bool rmBlob(size_t blobIndex)
  {
    auto refcnt = blobs_[blobIndex]->refcnt();
    if (refcnt == 1) { // only me is still holding reference
      blobs_.remove(blobIndex);
      return true;
    } else {
      intrusiveRelease(blobs_[blobIndex].get());
    }
    return false;
  }

  SharedBlobPtr get(size_t id) const { return blobs_[id]; }

  void countMemory(size_t& sharedBytes, size_t& unsharedBytes) const
  {
    unsharedBytes = sizeof(blobs_);
    unsharedBytes += blobs_.keys().capacity() * (sizeof(String)+sizeof(SharedBlobPtr));
    sharedBytes = 0;

    for (auto const& b: blobs_) {
      if (b) {
        if (b->refcnt()==1)
          unsharedBytes += b->size;
        else
          sharedBytes += b->size;
      }
    }
  }

protected:
  struct KeyHash
  {
    size_t operator()(Key const& k) const { return k.hash; }
  };
  struct KeyEqual
  {
    bool operator()(Key const& a, Key const& b) const
    {
      return a.size == b.size && memcmp(a.data, b.data, a.size) == 0;
    }
  };

  LinearMap<Key, SharedBlobPtr, KeyHash, KeyEqual> blobs_;
};

class BlobDataCloumnImpl
    : public DataColumn
    , public BlobDataInterface
    , public CopyInterface
    , public ObjectTracker<BlobDataCloumnImpl>
{
  friend class BlobInterfaceImpl;
public:
  BlobDataCloumnImpl(String const& name, DataColumnDesc const& desc)
      : DataColumn(name, desc)
      , storage_(new BlobStorage)
      , idsInsideStorage_(new SharedVector<size_t>)
      , stringInterface_(this)
      , compareInterface_(this)
  {}
  ~BlobDataCloumnImpl()
  {
    for (size_t idx : *idsInsideStorage_)
      if (idx != -1)
        storage_->rmBlob(idx);
  }
  OVERRIDE_NEW_DELETE;

  //class BlobInterfaceImpl : public BlobDataInterface
  //{
  //private:
  //  BlobDataCloumnImpl* column_;
  //  friend class BlobDataCloumnImpl;

  //public:
  //  BlobInterfaceImpl(BlobDataCloumnImpl* column) : column_(column) {}

  //  bool setBlobData(CellIndex index, void const* data, size_t size) override
  //  {
  //    return column_->setBlobData(index, data, size);
  //  }
  //  size_t getBlobSize(CellIndex index) const override { return column_->getBlobSize(index); }
  //  bool   getBlobData(CellIndex index, void* data, size_t& size) const override
  //  {
  //    return column_->getBlobData(index, data, size);
  //  }

  //  SharedBlobPtr getBlob(CellIndex index) const override { return column_->getBlob(index); }
  //  bool          setBlob(CellIndex index, SharedBlobPtr blob) override
  //  {
  //    return column_->setBlob(index, blob);
  //  }
  //};

  class StringInterfaceImpl : public StringDataInterface
  {
  private:
    BlobDataCloumnImpl* column_;

  public:
    StringInterfaceImpl(BlobDataCloumnImpl* column) : column_(column) {}

    bool setString(CellIndex index, StringView const& str) override
    {
      return column_->setBlobData(index, str.data(), str.size());
    }
    StringView getString(CellIndex index) const override
    {
      auto blob = column_->getBlob(index);
      if (blob)
        return StringView(static_cast<char const*>(blob->data), blob->size);
      else
        return StringView(reinterpret_cast<char const*>(column_->desc().defaultValue.data()), column_->desc().defaultValue.size());
    }
  };

  class CompareInterfaceImpl : public CompareInterface
  {
  private:
    BlobDataCloumnImpl* self_;

  public:
    CompareInterfaceImpl(BlobDataCloumnImpl* self) : self_(self) {}

    bool comparable(DataColumn const* column) const override
    {
      return column->asBlobData() != nullptr;
    }

    static int cmp(SharedBlobPtr const& a, SharedBlobPtr const& b)
    {
      int c = std::memcmp(a->data, b->data, std::min(a->size, b->size));
      if (c != 0)
        return c;
      return a->size<b->size ? -1 : a->size>b->size ? 1 : 0;
    }

    int compare(CellIndex a, CellIndex b) const override
    {
      return cmp(self_->getBlob(a), self_->getBlob(b));
    }

    int compare(CellIndex a, DataColumn const* that, CellIndex b) const override
    {
      DEBUG_ASSERT(comparable(that));
      return cmp(self_->getBlob(a), that->asBlobData()->getBlob(b));
    }

    bool searchable(DataType dt, sint tupleSize, size_t size) const override
    {
      return dt == self_->dataType();
    }

    CellIndex search(DataTable const* table, DataType dt, void const* data, size_t size) const override
    {
      DEBUG_ASSERT(searchable(dt, 0, size));
      auto const& ids = *self_->idsInsideStorage_;
      if (!!data && !!size) {
        size_t hash = xxhash(data, size);
        BlobStorage::Key key = { hash, data, size };
        struct PubBlobStorage : public BlobStorage {
          auto const& blobs() const { return blobs_; }
        } const* storage = static_cast<PubBlobStorage const*>(self_->storage_.get());
        if (auto idx = storage->blobs().indexof(key); idx != -1) {
          for (size_t i = 0; i < ids.size(); ++i) {
            if (size_t id = ids[i]; id == idx && table->getRow(CellIndex(i)) != -1) {
              return CellIndex(i);
            }
          }
        }
      } else {
        for (size_t i = 0; i < ids.size(); ++i) {
          if (size_t id = ids[i]; id == -1 && table->getRow(CellIndex(i)) != -1) {
            return CellIndex(i);
          }
        }
      }
      return CellIndex(-1);
    }

    size_t searchAll(Vector<CellIndex>& outMatches, DataTable const* table, DataType dt, void const* data, size_t size) const override
    {
      DEBUG_ASSERT(searchable(dt, 0, size));
      auto const& ids = *self_->idsInsideStorage_;
      size_t cnt = 0;
      if (!!data && !!size) {
        size_t hash = xxhash(data, size);
        BlobStorage::Key key = { hash, data, size };
        struct PubBlobStorage : public BlobStorage {
          auto const& blobs() const { return blobs_; }
        } const* storage = static_cast<PubBlobStorage const*>(self_->storage_.get());
        if (auto idx = storage->blobs().indexof(key); idx != -1) {
          for (size_t i = 0; i < ids.size(); ++i) {
            if (size_t id = ids[i]; id == idx && table->getRow(CellIndex(id)) != -1) {
              outMatches.emplace_back(id);
              ++cnt;
            }
          }
        }
      } else {
        for (size_t i = 0; i < ids.size(); ++i) {
          if (size_t id = ids[i]; id == -1 && table->getRow(CellIndex(i)) != -1) {
            outMatches.emplace_back(i);
          }
        }
      }
      return cnt;
    }
  };

  // copy interface {{{
  bool copyable(DataColumn const* that) const override { return !!that->asBlobData(); }
  bool copy(CellIndex a, CellIndex b) override
  {
    return setBlob(a, getBlob(b));
  }
  bool copy(CellIndex a, DataColumn const* that, CellIndex b) override
  {
    if (auto bdi = that->asBlobData()) {
      return setBlob(a, bdi->getBlob(b));
    }
    return false;
  }
  // copy interface }}}

  NumericDataInterface*    asNumericData() override { return nullptr; }
  FixSizedDataInterface* asFixSizedData() override { return nullptr; }
  BlobDataInterface*       asBlobData() override { return this; }
  StringDataInterface*     asStringData() override { return desc_.dataType == DataType::STRING ? &stringInterface_ : nullptr; }
  CompareInterface const*  compareInterface() const override { return &compareInterface_; }
  CopyInterface*           copyInterface() override { return this; }

  size_t length() const override { return idsInsideStorage_->size(); }

  void reserve(size_t length) override
  {
    ASSERT(isUnique());
    size_t sizebefore = idsInsideStorage_->size();
    if (length < sizebefore)
      return;
    if (length == idsInsideStorage_->size() + 1) // one at a time -> push back
      idsInsideStorage_->push_back(-1);
    else {
      idsInsideStorage_->resize(length);
      for (size_t i = sizebefore; i < length; ++i) {
        (*idsInsideStorage_)[i] = -1;
      }
    }
  }

  DataColumn* join(DataColumn const* their) override
  {
    ASSERT(isUnique());
    size_t oldlength = length();
    reserve(oldlength + their->length());
    if (auto *bi=their->asBlobData()) {
      for (CellIndex idx(0); idx<their->length(); ++idx) {
        if (auto blob=bi->getBlob(idx))
          setBlob(idx+oldlength, blob);
      }
    }
    return this;
  }

  void move(CellIndex dst, CellIndex src, size_t count) override
  {
    RUNTIME_CHECK(isUnique(), "Trying to modify shared column \"{}\", refcnt = {}", name_, idsInsideStorage_->refcnt());
    size_t const oldlength      = length();
    size_t const srcStartOffset = src.value();
    size_t const srcEndOffset   = src.value() + count;
    size_t const dstStartOffset = dst.value();
    size_t const dstEndOffset   = dst.value() + count;

    // nothing to do
    if (srcStartOffset == dstStartOffset)
      return;

    reserve(std::max(srcEndOffset, dstEndOffset));
    // use std::memmove will mess up reference counting
    if (srcEndOffset > dstStartOffset) {
      // when src's back overlaps dst's front, we'll need to move from back to front:
      // ---------SRC---------
      //               ---------DST---------
      for (size_t i=1; i<=count; ++i) {
        moveBlobByIndex(CellIndex(dstEndOffset-i), CellIndex(srcEndOffset-i));
      }
    } else {
      //               ---------SRC---------
      // ---------DST---------
      for (size_t i=0; i<count; ++i) {
        moveBlobByIndex(CellIndex(dstStartOffset+i), CellIndex(srcStartOffset+i));
      }
    }
  }

  DataColumnPtr clone() const override
  {
    auto shared = share();
    shared->makeUnique();
    return shared;
  }

  String toString(CellIndex index, sint lengthLimit) const override
  {
    auto blob = getBlob(index);
    if (!blob) {
      if (dataType() == DataType::STRING)
        return String(desc_.defaultValue.begin(), desc_.defaultValue.end());
      else
        return "#N/A#";
    } else if (blob->size==0) {
      return "";
    }
    uint8_t const* data = static_cast<uint8_t const*>(blob->data);
    size_t dispSize = std::min<size_t>(blob->size, lengthLimit>0 ? lengthLimit : 1024); // TODO: configurable max display length
    size_t acceptedTextLength = 0;
    bool isText = true;
    // utf8 rules
    for (size_t i=0; i<dispSize;) {
      uint8_t c = data[i];
      int octet = 0;
      if (c<=0x1f && c!='\t' && c!='\r' && c!='\n') { // control characters
        isText = false;
      } else if ((c & 0x80) == 0) {    // 0XXXXXXX, ascii
        octet = 1;
      } else if ((c & 0xE0) == 0xC0) { // 110XXXXX
        octet = 2;
      } else if ((c & 0xF0) == 0xE0) { // 1110XXXX
        octet = 3;
      } else if ((c & 0xF8) == 0xF0) { // 11110XXX
        octet = 4;
      } else {
        isText = false;
      }
      if (i+octet <= dispSize) {
        for (++i;--octet && i<dispSize;++i) {
          if ((data[i]&0x80) != 0x80)
            isText = false;
        }
      } else {
        break;
      }
      if (!isText)
        break;
      else
        acceptedTextLength = i;
    }
    if (isText && acceptedTextLength>0) {
      auto result = String(data, data+acceptedTextLength);
      if (acceptedTextLength<blob->size)
        return result + "...";
      else
        return result;
    } else {
      return fmt::format("{} bytes non-utf8 blob", getBlobSize(index));
    }
  }

  DataColumnPtr share() const override { return new BlobDataCloumnImpl(*this); }

  void makeUnique() override
  {
    if (isUnique())
      return;
    PROFILER_SCOPE_DEFAULT();
    storage_          = new BlobStorage(*storage_);
    idsInsideStorage_ = new SharedVector<size_t>(*idsInsideStorage_);
  }

  bool isUnique() const override { return idsInsideStorage_->refcnt() == 1; }

  size_t shareCount() const override { return idsInsideStorage_->refcnt(); }

  void defragment(DefragmentInfo const& how) override
  {
    ASSERT(isUnique());
    auto& idmap = *idsInsideStorage_;
    for (auto const& op : how.operations()) {
      switch (op.op) {
      case DefragmentInfo::OpCode::MOVE: {
        // idmap[op.args[1]] = idmap[op.args[0]];
        moveBlobByIndex(CellIndex(op.args[1]), CellIndex(op.args[0]));
        break;
      }
      case DefragmentInfo::OpCode::REMOVE: {
        auto& id = idmap[op.args[0]]; 
        if (id != -1)
          storage_->rmBlob(id);
        id = -1;
        break;
      }
      default:
        break;
      }
    }
    idsInsideStorage_->resize(how.finalSize());
    idsInsideStorage_->shrink_to_fit();
  }

  void countMemory(size_t& sharedBytes, size_t& unsharedBytes) const override
  {
    unsharedBytes = sizeof(*this);
    sharedBytes = 0;

    if (idsInsideStorage_->refcnt()==1)
      unsharedBytes += idsInsideStorage_->capacity() * sizeof(size_t);
    else
      sharedBytes += idsInsideStorage_->capacity() * sizeof(size_t);

    size_t blobSharedSize=0, blobUnsharedSize=0;
    storage_->countMemory(blobSharedSize, blobUnsharedSize);

    sharedBytes += blobSharedSize;
    if (storage_->refcnt()==1) {
      unsharedBytes += blobUnsharedSize;
    } else {
      sharedBytes += blobUnsharedSize;
    }
  }

protected:
  bool setBlobData(CellIndex index, void const* data, size_t size) override
  {
    PROFILER_SCOPE_DEFAULT();
    RUNTIME_CHECK(isUnique(), "Trying to modify shared column \"{}\", refcnt = {}", name_, idsInsideStorage_->refcnt());
    RUNTIME_CHECK(index.valid(), "Invalid index: {}", index.value());
    size_t idToRemove = -1;
    if (index < idsInsideStorage_->size()) {
      // in case of new blob is identical to previous version
      // we remove this reference later
      idToRemove = (*idsInsideStorage_)[index.value()];
    }
    size_t newid = storage_->addBlob(data, size);
    ensureVectorSize(*idsInsideStorage_, index.value() + 1, -1);
    (*idsInsideStorage_)[index.value()] = newid;
    if (idToRemove != -1) {
      storage_->rmBlob(idToRemove);
    }
    return true;
  }
  size_t getBlobSize(CellIndex index) const override
  {
    RUNTIME_CHECK(index.valid(), "Invalid index: {}", index.value());
    if (index < idsInsideStorage_->size()) {
      auto storeid = (*idsInsideStorage_)[index.value()];
      if (storeid != -1)
        if (auto blobptr = storage_->get(storeid))
          return blobptr->size;
    }
    return 0;
  }
  bool setBlob(CellIndex index, SharedBlobPtr blob) override
  {
    RUNTIME_CHECK(isUnique(), "Trying to modify shared column \"{}\", refcnt = {}", name_, idsInsideStorage_->refcnt());
    RUNTIME_CHECK(index.valid(), "Invalid index: {}", index.value());
    size_t idToRemove = -1;
    if (index < idsInsideStorage_->size()) {
      // in case of new blob is identical to previous version
      // we remove this reference later
      idToRemove = (*idsInsideStorage_)[index.value()];
    }
    size_t newid = storage_->addBlob(blob);
    ensureVectorSize(*idsInsideStorage_, index.value() + 1, -1);
    (*idsInsideStorage_)[index.value()] = newid;
    if (idToRemove != -1) {
      storage_->rmBlob(idToRemove);
    }
    return true;
  }
  SharedBlobPtr getBlob(CellIndex index) const override
  {
    RUNTIME_CHECK(index.valid(), "Invalid index: {}", index.value());
    if (index < idsInsideStorage_->size()) {
      auto storeid = (*idsInsideStorage_)[index.value()];
      if (storeid != -1)
        return storage_->get(storeid);
    }
    return nullptr;
  }
  bool getBlobData(CellIndex index, void* data, size_t& size) const override
  {
    RUNTIME_CHECK(index.valid(), "Invalid index: {}", index.value());
    if (index < idsInsideStorage_->size()) {
      auto storeid = (*idsInsideStorage_)[index.value()];
      if (storeid != -1) {
        if (auto blobptr = storage_->get(storeid)) {
          size = blobptr->size;
          memcpy(data, blobptr->data, blobptr->size);
          return true;
        }
      }
    }
    size = 0;
    return false;
  }

  void moveBlobByIndex(CellIndex dst, CellIndex src)
  {
    auto& idmap = *idsInsideStorage_;
    DEBUG_ASSERT(dst.value()<idmap.size());
    DEBUG_ASSERT(src.value()<idmap.size());
    if (dst == src)
      return;
    auto idx = idmap[dst.value()];
    if (idx != -1)
      storage_->rmBlob(idx);
    idmap[dst.value()] = idmap[src.value()];
    idmap[src.value()] = -1;
  }

protected:
  BlobDataCloumnImpl(BlobDataCloumnImpl const& that)
      : DataColumn(that.name(), that.desc())
      , storage_(that.storage_)
      , idsInsideStorage_(that.idsInsideStorage_)
      , stringInterface_(this)
      , compareInterface_(this)
  {
    for(auto id:*idsInsideStorage_) {
      if(id!=-1)
        intrusiveAddRef(storage_->get(id).get());
    }
  }
  IntrusivePtr<BlobStorage>          storage_;
  IntrusivePtr<SharedVector<size_t>> idsInsideStorage_;
  StringInterfaceImpl                stringInterface_;
  CompareInterfaceImpl               compareInterface_;
};

// }}} Blob Interface

}

END_JOYFLOW_NAMESPACE
