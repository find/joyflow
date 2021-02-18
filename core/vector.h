#pragma once

#include "def.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>

BEGIN_JOYFLOW_NAMESPACE

constexpr size_t VECTOR_INITIAL_CAPACITY = 4;

/// yet another vector implementation
///
/// differences between std::vector:
/// * our allocator does NOT match std::allocator,
///   because 1) we are using realloc whenever possible
///           2) we need aligned alloc
/// * able to handle aligned type
/// * empty vectors are totally free
/// * for trivial types, use memmove / memcpy when possible
/// * push_back returns iterator
/// * pop_back returns poped value
///
/// your allocator should have signature like this:
/// ```
/// struct MyAllocator
/// {
///   static void* malloc(size_t size);
///   static void* realloc(void* p, size_t newsize);
///   static void  free(void* p);
///
///   static void* aligned_alloc(size_t size, size_t align);
///   static void* aligned_realloc(void* p, size_t newsize, size_t align);
///   static void  aligned_free(void* p, size_t align);
/// };
/// ```
template<class T, class Alloc = Allocator>
class Vector
{
  T*     ptr_      = nullptr;
  size_t size_     = 0;
  size_t capacity_ = 0;

  static constexpr size_t stride = sizeof(T);
  static constexpr size_t align  = alignof(T);

public:
  typedef T                                     value_type;
  typedef T*                                    pointer;
  typedef T&                                    reference;
  typedef T const*                              const_pointer;
  typedef T const&                              const_reference;
  typedef const_pointer                         const_iterator;
  typedef pointer                               iterator;
  typedef size_t                                size_type;
  typedef intptr_t                              ssize_type;
  typedef std::reverse_iterator<iterator>       reverse_iterator;
  typedef std::reverse_iterator<const_iterator> const_reverse_iterator;
  typedef std::true_type                        tag_is_vector;

public:
  Vector() : ptr_(nullptr), size_(0), capacity_(0) {}

  explicit Vector(size_type count) { resize(count); }

  Vector(size_type count, const T& val)
      : ptr_(static_cast<T*>(Alloc::aligned_alloc(count * stride, align)))
      , size_(count)
      , capacity_(count)
  {
    for (T *p = ptr_, *e = ptr_ + size_; p < e; ++p)
      new (p) T(val);
  }

  Vector(std::initializer_list<T> il)
      : ptr_(static_cast<T*>(Alloc::aligned_alloc(il.size() * stride, align)))
      , size_(il.size())
      , capacity_(il.size())
  {
    auto iitr = il.begin();
    for (T *p = ptr_, *e = ptr_ + size_; p < e; ++p) {
      new (p) T(*iitr);
      ++iitr;
    }
  }

  template <class Iter>
  Vector(Iter begin, Iter end) {
    assign(begin, end);
  }

  Vector(Vector const& v)
      : ptr_(v.size() ? static_cast<T*>(Alloc::aligned_alloc(v.size() * stride, align)) : nullptr)
      , size_(v.size())
      , capacity_(v.size())
  {
    if constexpr (std::is_trivial<T>::value) {
      if (ptr_ && v.ptr_)
        std::memcpy(ptr_, v.ptr_, size_ * stride);
    }  else {
      T const* srcitr = v.ptr_;
      for (T *p = ptr_, *e = ptr_ + size_; p < e; ++p) {
        new (p) T(*srcitr);
        ++srcitr;
      }
    }
  }

  Vector(Vector&& v) noexcept : ptr_(v.ptr_), size_(v.size_), capacity_(v.capacity_)
  {
    v.ptr_      = nullptr;
    v.size_     = 0;
    v.capacity_ = 0;
  }

  ~Vector()
  {
    if (ptr_) {
      if constexpr (!std::is_trivial<T>::value)
        for (T *p = ptr_, *e = ptr_ + size_; p < e; ++p)
          p->~T();
      Alloc::aligned_free(ptr_, align);
    }
  }

  const_iterator begin() const { return ptr_; }
  const_iterator end() const { return ptr_ + size_; }
  iterator       begin() { return ptr_; }
  iterator       end() { return ptr_ + size_; }

  const_reverse_iterator rbegin() const { return const_reverse_iterator(end()); }
  const_reverse_iterator rend() const { return const_reverse_iterator(begin()); }
  reverse_iterator       rbegin() { return reverse_iterator(end()); }
  reverse_iterator       rend() { return reverse_iterator(begin()); }

  size_type  size() const { return size_; }
  ssize_type ssize() const { return size_; } // signed size
  size_type  empty() const { return size_ == 0; }
  size_type  capacity() const { return capacity_; }

  T&       front() { return *ptr_; }
  T const& front() const { return *ptr_; }
  T&       back() { return ptr_[size_ - 1]; }
  T const& back() const { return ptr_[size_ - 1]; }

  void assign(size_type count, const T& value)
  {
    if constexpr (!std::is_trivial<T>::value)
      for (T *p = ptr_, *e = ptr_ + size_; p < e; ++p)
        p->~T();
    for (T *p = ptr_, *e = ptr_ + count; p < e; ++p) {
      new (p) T(value);
    }
    size_ = count;
  }
  template<class Iterator>
  void assign(Iterator first, Iterator last)
  {
    if constexpr (!std::is_trivial<T>::value)
      for (T *p = ptr_, *e = ptr_ + size_; p < e; ++p)
        p->~T();
    resize(last - first);
    for (T *p = ptr_, *e = ptr_ + size_; p < e; ++p) {
      new (p) T(*first);
      ++first;
    }
  }
  void assign(std::initializer_list<T> il) { assign(il.begin(), il.end()); }

  T& at(size_type idx)
  {
    if (idx >= size_)
      throw std::out_of_range("vector index out-of-range");
    return ptr_[idx];
  }
  T const& at(size_type idx) const
  {
    if (idx >= size_)
      throw std::out_of_range("vector index out-of-range");
    return ptr_[idx];
  }

  T&       operator[](size_type idx) { return ptr_[idx]; }
  T const& operator[](size_type idx) const { return ptr_[idx]; }

  Vector& operator=(Vector const& rhs)
  {
    this->~Vector();
    new (this) Vector(rhs);
    return *this;
  }
  Vector& operator=(Vector&& rhs) noexcept
  {
    this->~Vector();
    new (this) Vector(rhs);
    return *this;
  }
  Vector& operator=(std::initializer_list<T> il)
  {
    this->~Vector();
    new (this) Vector(il);
    return *this;
  }

  T const* data() const { return ptr_; }
  T*       data() { return ptr_; }

  void reserve(size_type cap)
  {
    if (cap > capacity_) {
      if constexpr (std::is_trivial<T>::value) {
        void* newptr = Alloc::aligned_realloc(ptr_, cap * stride, align);
        if (!newptr)
          throw std::bad_alloc();
        ptr_ = static_cast<T*>(newptr);
      } else { // non-trivial
        T* newptr = static_cast<T*>(Alloc::aligned_alloc(cap * stride, align));
        if (!newptr)
          throw std::bad_alloc();
        for (T *p = ptr_, *np = newptr, *e = ptr_ + size_; p < e; ++p, ++np) {
          new (np) T(std::move(*p));
          p->~T();
        }
        Alloc::aligned_free(ptr_, align);
        ptr_ = newptr;
      }
      capacity_ = cap;
    }
  }

  void shrink_to_fit()
  {
    if (capacity_ > size_) {
      T* newptr = static_cast<T*>(Alloc::aligned_alloc(size_ * stride, align));
      if constexpr (std::is_trivial<T>::value) {
        std::memmove(newptr, data(), size_ * stride);
      } else {
        for (T *p = ptr_, *np = newptr, *e = ptr_ + size_; p < e; ++p, ++np) {
          new (np) T(std::move(*p));
          p->~T();
        }
      }
      Alloc::aligned_free(ptr_, align);
      ptr_      = newptr;
      capacity_ = size_;
    }
  }

  void clear()
  {
    if constexpr (!std::is_trivial<T>::value)
      for (T *p = ptr_, *e = ptr_ + size_; p < e; ++p) {
        p->~T();
      }
    size_ = 0;
  }

  void resize(size_type size)
  {
    if (size == size_)
      return;
    if (size < size_) {
      if constexpr (!std::is_trivial<T>::value)
        for (T *p = ptr_ + size, *e = ptr_ + size_; p < e; ++p)
          p->~T();
    } else {
      reserve(size);
      if constexpr (std::is_trivial<T>::value) {
        std::memset(ptr_+size_, 0, (size-size_)*sizeof(T));
        size_ = size;
      } else {
        for (; size_ < size; emplace_back())
          ;
      }
    }
    size_ = size;
  }

  iterator insert(iterator pos, const T& value)
  {
    if (pos < begin() || pos > end())
      throw std::out_of_range("pos not inside container");
    grow(1);
    if constexpr (std::is_trivial<T>::value) {
      std::memmove(pos + 1, pos, stride * (end() - pos));
    } else {
      for (T *p = end(), *e = pos; p > e; --p)
        new (p) T(std::move(*(p - 1)));
    }
    new (pos) T(value);
    ++size_;
    return pos;
  }

  iterator insert(const_iterator pos, const T& value)
  {
    iterator mutpos = ptr_ + (pos - ptr_);
    return insert(mutpos, value);
  }

  iterator erase(iterator pos)
  {
    if (pos < begin() || pos >= end())
      throw std::out_of_range("pos not inside container");
    if constexpr (std::is_trivial<T>::value) {
      std::memmove(pos, pos + 1, stride * (end() - pos) - 1);
    } else {
      pos->~T();
      for (T *i = pos+1, *e = ptr_+size_; i<e; ++i)
        new (i-1) T(std::move(*i));
    }
    --size_;
    return pos;
  }
  iterator erase(const_iterator pos)
  {
    iterator mutpos = ptr_ + (pos - ptr_);
    return erase(mutpos);
  }

  template<class... Arg>
  reference emplace_back(Arg&&... arg)
  {
    grow(1);
    T* p = ptr_ + size_;
    new (p) T(std::forward<Arg>(arg)...);
    ++size_;
    return *p;
  }

  iterator push_back(const T& value)
  {
    grow(1);
    T* p = ptr_ + size_;
    new (p) T(value);
    ++size_;
    return p;
  }

  iterator push_back(T&& value)
  {
    grow(1);
    T* p = ptr_ + size_;
    new (p) T(value);
    ++size_;
    return p;
  }

  T pop_back()
  {
    if (size_ == 0)
      throw std::out_of_range("nothing to pop");
    T val = std::move(ptr_[--size_]);
    (ptr_ + size_)->~T();
    return val;
  }

private:
  void grow(size_t size)
  {
    if (size_ + size > capacity_)
      reserve(
        std::max(
          VECTOR_INITIAL_CAPACITY,
          std::max(
            size_ + size, // grow 1.5x each time
            static_cast<size_type>(capacity_ + (capacity_>>1)))));
  }
};

END_JOYFLOW_NAMESPACE
