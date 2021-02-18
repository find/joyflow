#include "datatable_detail.h"
#include "datacolumn_numeric.h"
#include "datacolumn_fixsized.h"
#include "datacolumn_container.h"
#include "datacolumn_blob.h"

BEGIN_JOYFLOW_NAMESPACE

namespace detail {

// IndexMap {{{

CellIndex IndexMap::rowToIndex(sint row) const
{
  if (isTrivial_)
    return size_t(row) >= numRows_ ? CellIndex(-1) : CellIndex(static_cast<size_t>(row));
  if (row < 0 || static_cast<size_t>(row) >= rowToIndex_.size())
    return CellIndex(-1);
  else
    return CellIndex(rowToIndex_[row]);
}

sint IndexMap::indexToRow(CellIndex index) const
{
  if (isTrivial_)
    return index >= numRows_ ? -1 : static_cast<sint>(index.value());
  if (index >= indexToRow_.size())
    return -1;
  else
    return indexToRow_[index.value()];
}

CellIndex IndexMap::addRow()
{
  ++numRows_;
  if (isTrivial_) {
    return CellIndex(numRows_ - 1);
  } else {
    size_t numrow = rowToIndex_.size();
    size_t numidx = indexToRow_.size();
    rowToIndex_.push_back(numidx);
    indexToRow_.push_back(numrow);

    return CellIndex(numidx);
  }
}

CellIndex IndexMap::addRows(size_t n)
{
  numRows_ += n;
  if (isTrivial_) {
    return CellIndex(numRows_ - n);
  } else {
    size_t numrow = rowToIndex_.size();
    size_t numidx = indexToRow_.size();
    rowToIndex_.reserve(numRows_);
    indexToRow_.reserve(numRows_);
    for (size_t i = 0, idx = numidx, row = numrow; i < n; ++i, ++idx, ++row) {
      rowToIndex_.push_back(idx);
      indexToRow_.push_back(row);
    }
    return CellIndex(numidx);
  }
}

void IndexMap::markRemoval(sint row)
{
  ALWAYS_ASSERT(row >= 0);
  if (static_cast<size_t>(row) >= numRows_ || numRows_ == 0)
    return;
  if (isTrivial_) {
    ensureVectorSize(rowToIndex_, numRows_);
    ensureVectorSize(indexToRow_, numRows_);
    for (size_t i = 0; i < numRows_; ++i) {
      indexToRow_[i] = i;
      rowToIndex_[i] = i;
    }
    isTrivial_ = false;
  }
  auto& idx = rowToIndex_[row];
  if (idx != -1)
    indexToRow_[idx] = -1;
  idx = -1;
}

void IndexMap::applyRemoval()
{
  PROFILER_SCOPE_DEFAULT();
  if (isTrivial_)
    return;
  size_t writecursor = 0;
  for (size_t readcursor = 0, n = rowToIndex_.size(); readcursor < n;
    ++readcursor, ++writecursor) {
    while (readcursor < n && rowToIndex_[readcursor] == -1)
      ++readcursor;
    if (readcursor == n)
      break;
    if (readcursor != writecursor)
      rowToIndex_[writecursor] = rowToIndex_[readcursor];
  }
  rowToIndex_.resize(writecursor);
  for (size_t i=0; i<rowToIndex_.size(); ++i) {
    indexToRow_[rowToIndex_[i]] = i;
  }
  numRows_ = writecursor;
}

void IndexMap::removeRow(sint row)
{
  ALWAYS_ASSERT(row >= 0);
  if (static_cast<size_t>(row) >= numRows_ || numRows_ == 0)
    return;
  --numRows_;
  if (isTrivial_) {
    if (static_cast<size_t>(row) != numRows_) { // now we really need to alloc the LUTs
      ensureVectorSize(rowToIndex_, numRows_ + 1);
      ensureVectorSize(indexToRow_, numRows_ + 1);
      for (size_t i = 0; i < numRows_ + 1; ++i) {
        indexToRow_[i] = i;
        rowToIndex_[i] = i;
      }
      indexToRow_[row] = -1;
      isTrivial_       = false;
    } else {
      return;
    }
  }
  ALWAYS_ASSERT(rowToIndex_[row] < indexToRow_.size());
  indexToRow_[rowToIndex_[row]] = -1;
  for (size_t i = row, n = rowToIndex_.size(); i + 1 < n; ++i)
    rowToIndex_[i] = rowToIndex_[i + 1];
  for (sint& i2r : indexToRow_)
    if (i2r >= row)
      --i2r;
  rowToIndex_.pop_back();
}

// TODO: test
size_t IndexMap::removeRows(sint row, size_t n)
{
  ALWAYS_ASSERT(row >= 0);
  if (static_cast<size_t>(row) >= numRows_ || numRows_ == 0)
    return 0;
  if (isTrivial_) {
    if (static_cast<size_t>(row + n) < numRows_) { // now we really need to alloc the LUTs
      ensureVectorSize(rowToIndex_, numRows_);
      ensureVectorSize(indexToRow_, numRows_);
      for (size_t i = 0; i < numRows_; ++i) {
        indexToRow_[i] = i;
        rowToIndex_[i] = i;
      }
      indexToRow_[row] = -1;
      isTrivial_       = false;
    } else {
      size_t dsize = std::min<size_t>(row + n, numRows_) - row;
      numRows_ -= dsize;
      return dsize;
    }
  }
  for (size_t i = 0; i < n && row + i < numRows_; ++i) {
    ALWAYS_ASSERT(rowToIndex_[row + i] < indexToRow_.size());
    indexToRow_[rowToIndex_[row + i]] = -1;
  }
  size_t dsize   = std::min<size_t>(row + n, numRows_) - row;
  size_t newsize = numRows_ - dsize;
  for (size_t i = row, s = rowToIndex_.size(); i + n < s; ++i)
    rowToIndex_[i] = rowToIndex_[i + n];
  rowToIndex_.resize(newsize);
  for (sint& i2r : indexToRow_)
    if (i2r >= row)
      i2r -= dsize;
  numRows_ = newsize;
  return dsize;
}

// TODO: test this
/// Remove holes in inside indices
/// after this operation, it's safe to shrink
/// data storage to num of rows
///
/// given:
///  I2R = 5 3 1 2 X 4 X 0
///  R2I = 7 2 3 1 5 0
/// expected:
///  I2R = 5 3 1 2 4 0
///  R2I = 5 2 3 1 4 0
bool IndexMap::defragment(DefragmentInfo& defrag)
{
  PROFILER_SCOPE_DEFAULT();
  // defrag = DefragmentInfo();
  if (isTrivial_)
    return false;

  size_t writecursor = 0;
  bool   trivialNow  = true;
  for (size_t readcursor = 0, n = indexToRow_.size(); readcursor < n;
       ++readcursor, ++writecursor) {
    for (;readcursor < n && indexToRow_[readcursor] == -1; ++readcursor) {
      defrag.remove(readcursor);
    }
    if (readcursor == n)
      break;
    if (readcursor != writecursor) {
      defrag.move(readcursor, writecursor);
      indexToRow_[writecursor] = indexToRow_[readcursor];
    } 
    if (indexToRow_[writecursor] != writecursor)
      trivialNow = false;
  }
  if (trivialNow) {
    indexToRow_ = {};
    rowToIndex_ = {};
    isTrivial_  = true;
  } else {
    for (size_t i = 0, n = writecursor; i < n; ++i)
      rowToIndex_[indexToRow_[i]] = i;
    indexToRow_.resize(writecursor);
    rowToIndex_.resize(writecursor);
  }
  numRows_ = writecursor;
  defrag.setFinalSize(writecursor);
  return true;
}

void IndexMap::join(IndexMap const& that)
{
  PROFILER_SCOPE_DEFAULT();
  if (isTrivial_ && that.isTrivial_) {
    numRows_ += that.numRows_;
    return;
  }

  size_t i2rSize = indexToRow_.size();
  size_t r2iSize = rowToIndex_.size();
  if (isTrivial_) { // so that they are not trivial
                    // our combination will not be trivial
    indexToRow_.resize(numRows_);
    rowToIndex_.resize(numRows_);
    for (size_t i=0; i<numRows_; ++i) {
      indexToRow_[i] = i;
      rowToIndex_[i] = i;
    }
    i2rSize = r2iSize = numRows_;
  }
  indexToRow_.resize(i2rSize + that.numIndices());
  rowToIndex_.resize(r2iSize + that.numRows());

  for (size_t i = 0, n = that.numIndices(); i < n; ++i) {
    auto row = that.indexToRow(CellIndex(i));
    indexToRow_[i2rSize + i] = row == -1 ? -1 : row + r2iSize;
  }
  for (size_t i = 0, n = that.numRows(); i < n; ++i) {
    auto idx = that.rowToIndex(i);
    rowToIndex_[r2iSize + i] = idx.valid() ? idx.value() + i2rSize : -1;
  }
  numRows_ += that.numRows_;
  isTrivial_ = false;
}

void IndexMap::sort(Vector<sint> const& rowOrder)
{
  ASSERT(rowOrder.size() == numRows_);
  // TODO: check duplicate row in rowOrder?

  bool stillTrivial = isTrivial_;

  Vector<size_t> newOrder(numRows_);
  for (size_t i=0, n=numRows_; i<n; ++i) {
    newOrder[i] = rowToIndex(rowOrder[i]).value();
    stillTrivial &= (newOrder[i]==i);
  }

  if (stillTrivial)
    return;

  rowToIndex_ = std::move(newOrder);
  if (isTrivial_) {
    indexToRow_.resize(numRows_);
  }
  for (size_t i=0, n=rowToIndex_.size(); i<n; ++i) {
    indexToRow_[rowToIndex_[i]] = i;
  }
  isTrivial_ = false;
}
// }}} IndexMap

// DataTable Impl {{{
DataTableImpl::DataTableImpl()
  : columns_(std::make_shared<LinearMap<String, DataColumnPtr>>())
  , indexMap_(std::make_shared<IndexMap>())
  , varMap_(std::make_shared<HashMap<String, std::any>>())
{
}

DataTableImpl::~DataTableImpl()
{}

CellIndex DataTableImpl::addRow()
{
  // RUNTIME_CHECK(isUnique(), "try to modify a shared table");
  makeUnique();
  size_t size = numIndices();
  for (auto col : *columns_) {
    col->makeUnique();
    col->reserve(size + 1);
  }
  return indexMap_->addRow();
}

CellIndex DataTableImpl::addRows(size_t n)
{
  // RUNTIME_CHECK(isUnique(), "try to modify a shared table");
  makeUnique();
  size_t size = numIndices();
  for (auto col : *columns_) {
    col->makeUnique();
    col->reserve(size + n);
  }
  return indexMap_->addRows(n);
}

void DataTableImpl::markRemoval(sint row)
{
  // RUNTIME_CHECK(isUnique(), "try to modify a shared table");
  makeUnique();
  indexMap_->markRemoval(row);
}

void DataTableImpl::applyRemoval()
{
  // RUNTIME_CHECK(isUnique(), "try to modify a shared table");
  makeUnique();
  indexMap_->applyRemoval();
}

void DataTableImpl::removeRow(sint row)
{
  // RUNTIME_CHECK(isUnique(), "try to modify a shared table");
  makeUnique();
  indexMap_->removeRow(row);
}

size_t DataTableImpl::removeRows(sint row, size_t n)
{
  // RUNTIME_CHECK(isUnique(), "try to modify a shared table");
  makeUnique();
  return indexMap_->removeRows(row, n);
}

void DataTableImpl::defragment()
{
  PROFILER_SCOPE("defragment", 0xf9723d);
  // RUNTIME_CHECK(isUnique(), "try to modify a shared table");
  makeUnique();
  DefragmentInfo defrag;
  if (indexMap_->defragment(defrag))
    for (auto column : *columns_) {
      column->makeUnique();
      column->defragment(defrag);
    }
}

DataColumn* DataTableImpl::createColumn(String const& name,
                                        DataColumnDesc const& desc,
                                        bool          overwriteExisting)
{
  // RUNTIME_CHECK(isUnique(), "try to modify a shared table");
  makeUnique();
  RUNTIME_CHECK(desc.isValid(), "invalid column desc");
  DataColumn* column = nullptr;
  if (!overwriteExisting) {
    column = getColumn(name);
    if (column)
      return column;
  }
  auto const dataType = desc.dataType;

  if (desc.container) {
    ASSERT(isNumeric(desc.dataType));
    column = new ContainerDataColumnImpl(name, desc);
  } else if (!desc.fixSized) {
    ASSERT(desc.dataType == DataType::BLOB || desc.dataType == DataType::STRING);
    column = new BlobDataCloumnImpl(name, desc);
  } else {
    switch (dataType) {
    case DataType::INT32:
    case DataType::UINT32:
      column = new NumericDataColumnImpl<int32_t>(name, desc);
      break;
    case DataType::INT64:
    case DataType::UINT64:
      column = new NumericDataColumnImpl<int64_t>(name, desc);
      break;
    case DataType::FLOAT:
      column = new NumericDataColumnImpl<float>(name, desc);
      break;
    case DataType::DOUBLE:
      column = new NumericDataColumnImpl<double>(name, desc);
      break;
    case DataType::STRUCTURE:
      column = new StructuredDataColumnImpl(name, desc);
      break;
    default:
      ALWAYS_ASSERT(!"unsupported data type for createColumn");
    }
  }
  if (column) {
    column->reserve(numIndices());
    columns_->insert(name, column);
  }
  return column;
}

bool DataTableImpl::renameColumn(String const& oldName, String const& newName, bool overwriteExisting)
{
  // RUNTIME_CHECK(isUnique(), "try to modify a shared table");
  makeUnique();
  auto* colptr = columns_->find(oldName);
  if (colptr == nullptr)
    return false;
  auto col = *colptr;
  if (columns_->find(newName) == nullptr)
    columns_->reset(columns_->indexof(oldName), newName, col);
  else if (overwriteExisting) {
    *columns_->find(newName) = col;
    columns_->remove(oldName);
    columns_->tighten();
  }
  col->rename(newName);
  return true;
}

bool DataTableImpl::removeColumn(String const& name)
{
  // RUNTIME_CHECK(isUnique(), "try to modify a shared table");
  makeUnique();
  if (columns_->find(name) == nullptr)
    return false;
  columns_->remove(name);
  columns_->tighten();
  return true;
}

void DataTableImpl::join(DataTable const* that)
{
  PROFILER_SCOPE_DEFAULT();
  // RUNTIME_CHECK(isUnique(), "try to modify a shared table");
  makeUnique();
  DataTableImpl const* their = static_cast<DataTableImpl const*>(that);
  size_t const oldlength = numIndices();

  // first process columns in our table
  for (size_t i=0, n=columns_->size(); i<n; ++i) {
    String const& key = columns_->key(i);
    (*columns_)[i]->makeUnique();
    if (auto* c=their->getColumn(key)) {
      (*columns_)[i] = (*columns_)[i]->join(c);
    } else {
      (*columns_)[i]->reserve(oldlength+their->numIndices());
    }
  }

  // then merge columns that doesn't exist in our table
  for (size_t i=0, n=their->columns_->size(); i<n; ++i) {
    String const& key = their->columns_->key(i);
    if (auto* c=getColumn(key)) {
      // already processed above, now pass
    } else {
      // create a new column that matches them
      auto theirClone = their->getColumn(key)->clone();
      theirClone->reserve(oldlength+their->numIndices());
      theirClone->move(CellIndex(oldlength), CellIndex(0), their->numIndices());
      columns_->insert(key, theirClone);
    }
  }
  indexMap_->join(*static_cast<DataTableImpl const*>(their)->indexMap_);
  for (auto const& kv : *their->varMap_) {
    if (varMap_->find(kv.first) == varMap_->end()) {
      varMap_->insert(kv);
    }
  }
}

DataTablePtr DataTableImpl::share()
{
  auto *tb = new DataTableImpl;
  tb->columns_ = columns_;
  tb->indexMap_ = indexMap_;
  tb->varMap_ = varMap_;
  return tb;
}

bool DataTableImpl::isUnique() const
{
  if (columns_.use_count() == 1 && indexMap_.use_count() == 1 && varMap_.use_count() == 1)
    return true;
  return false;
}

size_t DataTableImpl::shareCount() const
{
  return indexMap_.use_count();
}

void DataTableImpl::makeUnique()
{
  if (isUnique())
    return;
  PROFILER_SCOPE("MakeUnique", 0xb14b28);
  columns_ = std::make_shared<LinearMap<String, DataColumnPtr>>(*columns_);
  for (auto& column: *columns_) {
    column = column->share();
  }
  indexMap_ = std::make_shared<IndexMap>(*indexMap_);
  varMap_ = std::make_shared<HashMap<String, std::any>>(*varMap_);
}

void DataTableImpl::countMemory(size_t& sharedBytes, size_t& unsharedBytes) const
{
  unsharedBytes = sizeof(*this) + (sizeof(String)+sizeof(DataColumnPtr))*columns_->keys().capacity();
  sharedBytes = 0;

  for (auto column: *columns_) {
    size_t columnSharedBytes = 0, columnUnsharedBytes = 0;
    column->countMemory(columnSharedBytes, columnUnsharedBytes);
    sharedBytes += columnSharedBytes;
    if (column->refcnt()==1) {
      unsharedBytes += columnUnsharedBytes;
    } else {
      sharedBytes += columnUnsharedBytes;
    }
  }
  if (indexMap_.use_count()==1) {
    unsharedBytes += indexMap_->countMemory();
  } else {
    sharedBytes += indexMap_->countMemory();
  }
}
// DataTable Impl }}}

// DataCollection Impl {{{

sint DataCollectionImpl::addTable()
{
  sint idx = static_cast<sint>(tables_.size());
  tables_.push_back(new DataTableImpl);
  return idx;
}

sint DataCollectionImpl::addTable(DataTable* dt)
{
  sint idx = static_cast<sint>(tables_.size());
  tables_.push_back(dt);
  return idx;
}

void DataCollectionImpl::reserveTables(sint n)
{
  while (tables_.ssize() < n)
    addTable();
}

void DataCollectionImpl::removeTable(sint table)
{
  if (table >= 0 && table < tables_.ssize()) {
    tables_.erase(tables_.begin()+table);
  }
}

void DataCollectionImpl::defragment()
{
  for (sint i = 0, n = static_cast<sint>(tables_.size()); i < n; ++i) {
    getTable(i)->defragment();
  }
}

DataCollectionPtr DataCollectionImpl::share()
{
  DataCollectionImpl* dc = new DataCollectionImpl;
  for (size_t i = 0, n = numTables(); i < n; ++i) {
    // sint t = dc->addTable();
    // ALWAYS_ASSERT(t == i);
    // auto& srcColumns = tables_[i]->columns_;
    // auto& dstColumns = dc->tables_[i]->columns_;
    // for (size_t c = 0; c < srcColumns.size(); ++c) {
    //   String const& name = srcColumns.keys()[c];
    //   dstColumns.insert(name, srcColumns[c]->share());
    // }

    dc->tables_.push_back(tables_[i]->share());
  }
  return dc;
}

DataCollectionImpl::~DataCollectionImpl()
{
}

void DataCollectionImpl::join(DataCollection const* that)
{
  for (size_t i=0, n=std::min(numTables(), that->numTables()); i<n; ++i) {
    tables_[i]->join(that->getTable(i));
  }
}

void DataCollectionImpl::countMemory(size_t& sharedBytes, size_t& unsharedBytes) const
{
  unsharedBytes = sizeof(*this);
  sharedBytes = 0;
  for (auto const& tb: tables_) {
    size_t tbSharedBytes = 0, tbUnsharedBytes = 0;
    tb->countMemory(tbSharedBytes, tbUnsharedBytes);
    sharedBytes += tbSharedBytes; 
    if (tb->refcnt()==1) {
      unsharedBytes += tbUnsharedBytes;
    } else {
      sharedBytes += tbUnsharedBytes;
    }
  }
}
// }}}

// Object Inspectors for stats {{{
static ObjectInspector dataColumnInspector() {
  static ObjectInspector inspector = {
    /*name*/ [](void const* obj) -> String {
      auto const* dc = static_cast<DataColumn const*>(obj);
      return dc->name();
    },
    /*size*/ [](void const* obj) -> size_t {
      auto const* dc = static_cast<DataColumn const*>(obj);
      size_t shared=0, unshared=0;
      dc->countMemory(shared, unshared);
      return shared+unshared;
    },
    /*shared size*/ [](void const* obj) -> size_t {
      auto const* dc = static_cast<DataColumn const*>(obj);
      size_t shared=0, unshared=0;
      dc->countMemory(shared, unshared);
      return shared;
    },
    /*ushared size*/ [](void const* obj) -> size_t {
      auto const* dc = static_cast<DataColumn const*>(obj);
      size_t shared=0, unshared=0;
      dc->countMemory(shared, unshared);
      return unshared;
    }
  };
  return inspector;
}

static struct InspectorRegister {
  InspectorRegister() {
    Stats::setInspector<NumericDataColumnImpl<int32_t>>(dataColumnInspector());
    Stats::setInspector<NumericDataColumnImpl<int64_t>>(dataColumnInspector());
    Stats::setInspector<NumericDataColumnImpl<float>>(dataColumnInspector());
    Stats::setInspector<NumericDataColumnImpl<double>>(dataColumnInspector());
    Stats::setInspector<StructuredDataColumnImpl>(dataColumnInspector());
    Stats::setInspector<BlobDataCloumnImpl>(dataColumnInspector());
    Stats::setInspector<ContainerDataColumnImpl>(dataColumnInspector());
  }
} _reg;
// Object Inspectors }}}

} // namespace detail;

// Compare Interface {{{
CompareInterface* CompareInterface::notComparable()
{
  struct NoneComparable : public CompareInterface
  {
    bool comparable(DataColumn const* that) const override { return false; }
    int  compare(CellIndex a, CellIndex b)  const override { return 0; }
    int  compare(CellIndex indexInThis, DataColumn const* that, CellIndex indexInThat) const override { return 0; }
    bool searchable(DataType dataType, sint tupleSize, size_t size) const override { return false; }
    CellIndex search(DataTable const*, DataType dataType, void const* data, size_t size) const override { return CellIndex(-1); }
    size_t    searchAll(Vector<CellIndex> &outMatches, DataTable const*, DataType dataType, void const* data, size_t size) const override { return 0; }
  };
  static std::unique_ptr<NoneComparable> instance_{ new  NoneComparable };
  return instance_.get();
}
// Relationship Interface }}}

// DataColumnDesc {{{
bool DataColumnDesc::isValid() const
{
  if (fixSized && !container && defaultValue.empty() && defaultValue.size()!=elemSize) {
    spdlog::warn("default value size mismatch");
    return false;
  }
  if (objCallback && !fixSized) {
    spdlog::warn("object storage can only work on fix-sized elements");
    return false;
  }
  if (objCallback && dataType!=DataType::STRUCTURE && dataType<DataType::CUSTOM) {
    spdlog::warn("object callback should not be set on builtin types");
    return false;
  }
  if (objCallback && container) {
    spdlog::warn("objects cannot be stored in container (yet)");
    return false;
  }
  if (container &&
      dataType != DataType::INT32 && 
      dataType != DataType::UINT32 && 
      dataType != DataType::INT64 && 
      dataType != DataType::UINT64 &&
      dataType != DataType::FLOAT && 
      dataType != DataType::DOUBLE && 
      dataType != DataType::STRUCTURE) {
    spdlog::warn("only numbers and fix-sized objects can be put into container");
    return false;
  }
  return true;
}

bool DataColumnDesc::compatible(DataColumnDesc const& that) const
{
  return dataType == that.dataType &&
    tupleSize == that.tupleSize &&
    elemSize == that.elemSize &&
    dense == that.dense &&
    fixSized == that.fixSized &&
    container == that.container &&
    objCallback == that.objCallback &&
    defaultValue.size() == that.defaultValue.size();
}
// DataColumnDesc }}}

CORE_API DataCollectionPtr newDataCollection()
{
  return new detail::DataCollectionImpl;
}

CORE_API void deleteDataCollection(DataCollection* dc)
{
  delete dc;
}

END_JOYFLOW_NAMESPACE
