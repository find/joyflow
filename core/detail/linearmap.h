#pragma once

#include <core/def.h>
#include <core/error.h>
#include <core/stringview.h>
#include <core/vector.h>
#include <algorithm>

BEGIN_JOYFLOW_NAMESPACE

/// Linear Map - A map with linear object storage
/// objects are stored in vector container, so can be accessed by integer ids
/// and iteration through them is very efficient
/// beside the linear storage, a hash map of key -> id was provided so that
/// objects can also be quickly looked up
template<class Key, class Value, class Hash = std::hash<Key>, class Equal = std::equal_to<Key>>
class LinearMap
{
  Vector<Key>                       keys_;
  Vector<Value>                     values_;
  Vector<size_t>                    holes_;
  HashMap<Key, size_t, Hash, Equal> indices_;

public:
  size_t size() const { return values_.size(); }
  size_t filledSize() const { return values_.size() - holes_.size(); }
  size_t insert(std::pair<Key, Value> const& keyvalue)
  {
    return insert(keyvalue.first, keyvalue.second);
  }
  size_t insert(Key const& key, Value const& value)
  {
    auto itr = indices_.find(key);
    if (itr != indices_.end()) {
      DEBUG_ASSERT(Equal()(keys_[itr->second],key));
      values_[itr->second] = value;
      return itr->second;
    }
    if (!holes_.empty()) {
      size_t idx = holes_.back();
      holes_.pop_back();
      values_[idx]  = value;
      keys_[idx]    = key;
      indices_[key] = idx;
      return idx;
    }
    size_t idx = values_.size();
    values_.push_back(value);
    keys_.push_back(key);
    indices_[key] = idx;
    return idx;
  }
  sint indexof(Key const& key) const
  {
    auto itr = indices_.find(key);
    if (itr == indices_.end())
      return -1;
    else
      return static_cast<sint>(itr->second);
  }
  template <class OtherKey>
  typename std::enable_if<std::is_constructible<Key,OtherKey>::value, sint>::type
  indexof(OtherKey const& key) const
  {
    return indexof(Key(key));
  }
  Value remove(Key const& key)
  {
    Value v   = {};
    auto  itr = indices_.find(key);
    if (itr != indices_.end()) {
      holes_.push_back(itr->second);
      std::swap(values_[itr->second], v);
      keys_[itr->second] = Key();
      indices_.erase(itr);
    }
    return v;
  }
  /// reset value&key in certain index
  void reset(size_t index, Key const& key, Value const& value)
  {
    DEBUG_ASSERT(index >= 0 && index < values_.size());
    indices_.erase(keys_[index]);
    values_[index] = value;
    keys_[index] = key;
    indices_[key] = index;
  }
  Value remove(size_t index)
  {
    DEBUG_ASSERT(index < values_.size());
    holes_.push_back(index);
    Value v = {};
    std::swap(values_[index], v);
    auto& k = keys_[index];
    indices_.erase(k);
    k = Key();
    return v;
  }
  Value& operator[](size_t index)
  {
    DEBUG_ASSERT(index < values_.size());
    return values_[index];
  }
  Value const& operator[](size_t index) const
  {
    DEBUG_ASSERT(index < values_.size());
    return values_[index];
  }
  Key const& key(size_t index) const
  {
    DEBUG_ASSERT(index < keys_.size());
    return keys_[index];
  }
  Value* find(Key const& key)
  {
    auto itr = indices_.find(key);
    if (itr != indices_.end())
      return &values_[itr->second];
    return nullptr;
  }
  template <class OtherKey>
  typename std::enable_if<std::is_constructible<Key,OtherKey>::value, Value*>::type
  find(OtherKey const& key)
  {
    return find(Key(key));
  }
  Value const* find(Key const& key) const
  {
    auto itr = indices_.find(key);
    if (itr != indices_.end())
      return &values_[itr->second];
    return nullptr;
  }
  template <class OtherKey>
  typename std::enable_if<std::is_constructible<Key,OtherKey>::value, Value const*>::type
  find(OtherKey const& key) const
  {
    return find(Key(key));
  }
  Vector<Key> const& keys() const { return keys_; }

  void clear()
  {
    keys_.clear();
    values_.clear();
    holes_.clear();
    indices_.clear();
  }

  /// pack items tightly together, removing holes
  /// !WILL CHANGE INDEX->VALUE RELATIONSHIP!
  void tighten()
  {
    if (holes_.empty())
      return;
    Vector<size_t> holes;
    std::swap(holes, holes_);

    std::sort(holes.begin(), holes.end());
    size_t wcursor = holes[0];
    for (size_t hcursor = 1, rcursor = wcursor + 1, nholes = holes.size();; ++rcursor, ++wcursor) {
      while (hcursor < nholes && holes[hcursor] == rcursor) {
        ++hcursor;
        ++rcursor;
      }
      if (rcursor >= values_.size())
        break;
      values_[wcursor]         = std::move(values_[rcursor]);
      keys_[wcursor]           = std::move(keys_[rcursor]);
      indices_[keys_[wcursor]] = wcursor;
    }
    DEBUG_ASSERT(wcursor == values_.size() - holes.size());
    values_.resize(wcursor);
    keys_.resize(wcursor);
  }

  auto begin() { return values_.begin(); }
  auto end() { return values_.end(); }
  auto begin() const { return values_.begin(); }
  auto end() const { return values_.end(); }
};

END_JOYFLOW_NAMESPACE
