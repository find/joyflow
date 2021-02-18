#pragma once
#include "../def.h"
#include "../datatable.h"
#include "../error.h"
#include "../stats.h"
#include "../profiler.h"

#include "linearmap.h"
#include "utility.h"

#include <fmt/format.h>
#include <glm/glm.hpp>
#include <mimalloc.h>
#include <cstdlib>
#include <cstring>
#include <algorithm>

BEGIN_JOYFLOW_NAMESPACE

// DefragmentInfo {{{
class DefragmentInfo
{
public:
  struct OpCode
  {
    enum
    {
      MOVE,
      REMOVE
    } op;
    size_t args[2];
  };

private:
  Vector<OpCode> operations_;
  size_t         finalSize_ = 0;

public:
  /// record the "move a to b" operation
  void move(size_t a, size_t b) { operations_.push_back(OpCode{OpCode::MOVE, {a, b}}); }
  /// record the "remove x" operation
  /// note: ALWAYS MOVE FIRST THEN REMOVE
  void remove(size_t x) { operations_.push_back(OpCode{OpCode::REMOVE, {x, 0}}); }

  auto const& operations() const { return operations_; }

  size_t finalSize() const { return finalSize_; }
  void setFinalSize(size_t sz) { finalSize_ = sz; }
};
// }}}

namespace detail {

// Index Map {{{

/// IndexMap converts row id and array index forward and backward
/// row id is continunous numeric value ranging from 0 to array length
/// with -1 donates invalid id
/// array index is the real index inside storage class
/// removing rows from table won't directly remove values, but just
/// make those values inaccessable, which will cause holes in storage,
/// this IndexMap can keep track of there correspondance; the holes
/// can be eliminated by calling defragment() from DataCollection
class IndexMap : public ObjectTracker<IndexMap>
{
private:
  bool           isTrivial_ = true;
  size_t         numRows_   = 0;
  Vector<size_t> rowToIndex_;
  Vector<sint>   indexToRow_;

public:
  OVERRIDE_NEW_DELETE

  IndexMap() {}
  ~IndexMap() {}

  size_t    numRows() const { return numRows_; }
  size_t    numIndices() const { return isTrivial_ ? numRows_ : indexToRow_.size(); }
  CellIndex rowToIndex(sint row) const;
  sint      indexToRow(CellIndex index) const;

  /// return:
  /// true if defragment is needed,
  /// false if everything is already clean
  bool defragment(DefragmentInfo&);

  CellIndex addRow();
  CellIndex addRows(size_t n);
  void      removeRow(sint row);
  size_t    removeRows(sint row, size_t n);
  void      markRemoval(sint row);
  void      applyRemoval();

  void join(IndexMap const& that);

  void sort(Vector<sint> const& rowOrder); // TODO

  size_t countMemory() const {
    return sizeof(*this) + sizeof(size_t)*rowToIndex_.capacity() + sizeof(sint)*indexToRow_.capacity();
  }
};

// }}} Index Map

// DataTableImpl {{{
class DataTableImpl
    : public DataTable
    , public ObjectTracker<DataTableImpl>
{
protected:
  std::shared_ptr<LinearMap<String, DataColumnPtr>> columns_;
  std::shared_ptr<IndexMap>                         indexMap_;
  std::shared_ptr<HashMap<String, std::any>>        varMap_;

  friend class DataCollectionImpl;

public:
  OVERRIDE_NEW_DELETE;

  DataTableImpl();
  ~DataTableImpl();

  sint numColumns() const override
  {
    return columns_->size();
  }

  Vector<String> columnNames() const override
  {
    return columns_->keys();
  }

  DataColumn* getColumn(String const& name) override
  {
    auto* col = columns_->find(name);
    if (!col)
      return nullptr;
    return col->get();
  }
  DataColumn const* getColumn(String const& name) const override
  {
    auto const* col = columns_->find(name);
    if (!col)
      return nullptr;
    return col->get();
  }

  DataColumn* setColumn(String const& name, DataColumn* col) override
  {
    RUNTIME_CHECK(col->refcnt() <= 1,  "DataTable::setColumn: don't pass me a shared column ptr");
    makeUnique();
    if (col->length() != numIndices()) {
      col->makeUnique();
      col->reserve(numIndices());
    }
    columns_->insert(name, col);
    return col;
  }

  DataColumn* createColumn(String const& name,
                           DataColumnDesc const& desc,
                           bool          overwriteExisting = false) override;

  bool renameColumn(String const& oldName, String const& newName, bool overwriteExisting) override;
  bool removeColumn(String const& name) override;

  /// append one row for each column of this table
  /// return the newly added row id
  CellIndex addRow() override;
  CellIndex addRows(size_t n) override;
  void      markRemoval(sint row) override;
  void      applyRemoval() override;
  void      removeRow(sint row) override;
  size_t    removeRows(sint row, size_t n) override;

  /// defragment: remove holes and make data packed dense
  void defragment() override;

  void sort(Vector<sint> const& order) override
  {
    PROFILER_SCOPE("Sort", 0xf9d367);
    RUNTIME_CHECK(isUnique(), "try to modify a shared table");
    indexMap_->sort(order);
  }

  /// get internal index of row number
  CellIndex getIndex(sint row) const override { return indexMap_->rowToIndex(row); }

  /// get row number from internal index
  sint getRow(CellIndex index) const override { return indexMap_->indexToRow(index); }

  size_t numRows() const override { return indexMap_->numRows(); }

  size_t numIndices() const override { return indexMap_->numIndices(); }

  DataTablePtr share() override;

  bool isUnique() const override;

  size_t shareCount() const override;

  void makeUnique() override;

  void join(DataTable const* that) override;

  void countMemory(size_t& sharedBytes, size_t& unsharedBytes) const override;

  HashMap<String, std::any> const& vars() const override { return *varMap_; }
  void setVariable(String const& key, std::any const& val) override
  {
    makeUnique();
    if (val.has_value())
      (*varMap_)[key] = val;
    else
      varMap_->erase(key);
  }
  std::any getVariable(String const& key) const override
  {
    if (auto itr = varMap_->find(key); itr != varMap_->end())
      return itr->second;
    return std::any{};
  }
};
// DataTableImpl }}}

// DataCollectionImpl {{{

/// DataCollection: a worksheet-like interface of data storage
/// * there can be variable number of tables in one data collection,
///   accessed by integer ids
/// * each table may contain many columns, accessed by names
class DataCollectionImpl
    : public DataCollection
    , public ObjectTracker<DataCollectionImpl>
{
protected:
  Vector<IntrusivePtr<DataTable>> tables_;

public:
  OVERRIDE_NEW_DELETE;
  ~DataCollectionImpl();

  sint addTable() override;
  sint addTable(DataTable* dt) override;
  void reserveTables(sint n) override;
  void removeTable(sint table) override;

  sint numTables() const override { return tables_.ssize(); }

  DataTable* getTable(sint table) override
  {
    RUNTIME_CHECK(table >= 0 && table < tables_.ssize(), "Table index ({}) out of range [0, {})", table, tables_.size());
    return tables_[table].get();
  }

  DataTable const* getTable(sint table) const override
  {
    RUNTIME_CHECK(table >= 0 && table < tables_.ssize(), "Table index ({}) out of range [0, {})", table, tables_.size());
    return tables_[table].get();
  }

  void defragment() override;

  /// make an COW copy
  DataCollectionPtr share() override;

  void join(DataCollection const*) override;

  void countMemory(size_t& sharedBytes, size_t& unsharedBytes) const override;
};

// }}} DataCollectionImpl

// Shared Data Storage {{{

template<class T>
class SharedVector
    : public ReferenceCounted<SharedVector<T>>
    , public Vector<T>
    , public ObjectTracker<SharedVector<T>>
{
public:
  SharedVector() = default;
  SharedVector(size_t sz) : ReferenceCounted<SharedVector<T>>(), Vector<T>(sz) {}
  SharedVector(SharedVector const& that)
      : ReferenceCounted<SharedVector<T>>(that), Vector<T>(static_cast<Vector<T> const&>(that))
  {}
  OVERRIDE_NEW_DELETE;
};

// }}} Shared Data Storage

} // namespace detail

END_JOYFLOW_NAMESPACE

