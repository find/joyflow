#pragma once

#include <atomic>
#include <cstdlib>
#include <functional>
#include <memory>

template<class T, class Deletor = std::default_delete<T>>
class ReferenceCounted;
template<typename T, class D>
inline void intrusiveAddRef(const ReferenceCounted<T, D>* ptr);
template<typename T, class D>
inline void intrusiveRelease(const ReferenceCounted<T, D>* ptr);

template<class T, class Deletor>
class ReferenceCounted
{
private:
  mutable std::atomic<size_t> refcnt_;

public:
  ReferenceCounted() : refcnt_(0) {}
  ReferenceCounted(ReferenceCounted const&) : refcnt_(0) {}
  ReferenceCounted& operator=(ReferenceCounted const&) { return *this; }
  size_t            refcnt() const { return refcnt_; }

protected:
  ~ReferenceCounted() = default;

  friend void intrusiveAddRef<T, Deletor>(const ReferenceCounted<T, Deletor>* ptr);
  friend void intrusiveRelease<T, Deletor>(const ReferenceCounted<T, Deletor>* ptr);
};

template<typename T, class D>
inline void intrusiveAddRef(const ReferenceCounted<T, D>* ptr)
{
  ++ptr->refcnt_;
}

template<typename T, class D>
inline void intrusiveRelease(const ReferenceCounted<T, D>* ptr)
{
  if (--ptr->refcnt_ == 0)
    D()(static_cast<T*>(const_cast<ReferenceCounted<T, D>*>(ptr)));
}

template<class T>
class IntrusivePtr
{
private:
  T* ptr_;

public:
  T* get() const { return ptr_; }
  T* detach()
  {
    T* ptr = ptr_;
    ptr_   = nullptr;
    return ptr;
  }
  T* operator->() const { return ptr_; }
  T& operator*() const { return *ptr_; }

public:
  IntrusivePtr() : ptr_(nullptr) {}
  IntrusivePtr(T* ptr, bool addref = true) : ptr_(ptr)
  {
    if (addref && ptr) {
      intrusiveAddRef(ptr);
    }
  }
  IntrusivePtr(IntrusivePtr const& rhs) : ptr_(rhs.ptr_)
  {
    if (ptr_) {
      intrusiveAddRef(ptr_);
    }
  }
  template<class U>
  IntrusivePtr(IntrusivePtr<U> const& rhs) : ptr_(rhs.get())
  {
    if (ptr_) {
      intrusiveAddRef(ptr_);
    }
  }
  IntrusivePtr(IntrusivePtr&& rhs) noexcept : ptr_(rhs.ptr_) { rhs.ptr_ = nullptr; }
  ~IntrusivePtr()
  {
    if (ptr_) {
      intrusiveRelease(ptr_);
    }
  }

  void swap(IntrusivePtr& rhs)
  {
    T* ptr     = rhs.ptr_;
    rhs.ptr_   = this->ptr_;
    this->ptr_ = ptr;
  }
  void reset(T* rhs = nullptr, bool addref = true) { IntrusivePtr(rhs, addref).swap(*this); }
  IntrusivePtr& operator=(IntrusivePtr&& rhs) noexcept
  {
    IntrusivePtr(rhs).swap(*this);
    return *this;
  }
  IntrusivePtr& operator=(IntrusivePtr const& rhs)
  {
    IntrusivePtr(rhs).swap(*this);
    return *this;
  }
  template<class U>
  IntrusivePtr& operator=(IntrusivePtr<U>&& rhs)
  {
    IntrusivePtr(rhs).swap(*this);
    return *this;
  }
  template<class U>
  IntrusivePtr& operator=(IntrusivePtr<U> const& rhs)
  {
    IntrusivePtr(rhs).swap(*this);
    return *this;
  }
  IntrusivePtr& operator=(T* rhs)
  {
    IntrusivePtr(rhs).swap(*this);
    return *this;
  }

       operator bool() const { return !!ptr_; }
  bool operator==(IntrusivePtr<T> const& rhs) const { return ptr_ == rhs.get(); }
  bool operator!=(IntrusivePtr<T> const& rhs) const { return ptr_ != rhs.get(); }
  bool operator==(T const* ptr) const { return ptr_ == ptr; }
  bool operator!=(T const* ptr) const { return ptr_ != ptr; }
  bool operator<(IntrusivePtr<T> const& rhs) const { return ptr_ < rhs.get(); }
  bool operator<(T const* ptr) const { return ptr_ < ptr; }
};

template<class T>
inline bool operator==(T const* ptr, IntrusivePtr<T> const& rhs)
{
  return ptr == rhs.get();
}
template<class T>
inline bool operator!=(T const* ptr, IntrusivePtr<T> const& rhs)
{
  return ptr != rhs.get();
}
template<class T>
inline bool operator<(T const* ptr, IntrusivePtr<T> const& rhs)
{
  return ptr < rhs.get();
}
namespace std {
template<class T>
struct hash<IntrusivePtr<T>>
{
  size_t operator()(IntrusivePtr<T> const& ptr) { return std::hash<T const*>()(ptr.get()); }
};
} // namespace std
