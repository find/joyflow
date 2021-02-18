#include "doctest/doctest.h"
#include "core/intrusiveptr.h"

#include <memory>

TEST_CASE("IntrusivePtr.Counter")
{
  bool x_destructed = false;
  struct X : public ReferenceCounted<X>
  {
    bool& x_destructed;
    X(bool& x_destructed) : x_destructed(x_destructed) {}
    ~X() { x_destructed = true; }
  };

  {
    IntrusivePtr<X> py = nullptr;
    {
      IntrusivePtr<X> px = new X(x_destructed);
      IntrusivePtr<X> pz = nullptr;
      CHECK(px->refcnt() == 1);
      py = px;
      pz.swap(px);
      CHECK(px.get() == nullptr);
      CHECK(pz->refcnt() == 2);

      IntrusivePtr<X> pw = std::move(pz);
      CHECK(pw->refcnt() == 2);

      IntrusivePtr<X> pa(pw);
      CHECK(pw->refcnt() == 3);

      auto pb = pa;
      CHECK(pw->refcnt() == 4);
    }
    CHECK(py->refcnt() == 1);
  }
  CHECK(x_destructed);
}
