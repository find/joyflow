#include "doctest/doctest.h"
#include "core/detail/linearmap.h"

TEST_CASE("LinearMap")
{
  using namespace joyflow;
  LinearMap<String, int*> tpmap;
  CHECK(tpmap.size() == 0);
  CHECK(tpmap.find("hello") == nullptr);
  CHECK(tpmap.insert("hello", new int(1314)) == 0);
  CHECK(**tpmap.find("hello") == 1314);
  auto ptr = tpmap.remove(0);
  CHECK(ptr != nullptr);
  CHECK(*ptr == 1314);
  delete ptr;

  CHECK(tpmap.find("hello") == nullptr);
  CHECK(tpmap.insert("foo", new int(43210)) == 0);
  CHECK(tpmap.insert("bar", new int(98765)) == 1);
  auto p43210 = tpmap.remove("foo");
  CHECK(tpmap.key(1) == "bar");
  CHECK(tpmap.indexof("bar") == 1);
  auto p98765 = tpmap.remove(1);
  CHECK(tpmap.indexof("bar") == -1);
  CHECK(*p43210 == 43210);
  CHECK(*p98765 == 98765);
  delete p43210;
  delete p98765;
  CHECK(tpmap.filledSize() == 0);
  CHECK(tpmap.size() == 2);

  LinearMap<int, int> imap;
  for (int i = 0; i < 12; ++i)
    imap.insert(i, i);
  imap.remove(3);
  imap.remove(4);
  imap.remove(6);
  imap.remove(8);
  imap.remove(9);
  imap.remove(11);
  imap.tighten();
  int desired[] = {0, 1, 2, 5, 7, 10};
  for (int i = 0; i < sizeof(desired) / sizeof(desired[0]); ++i) {
    CHECK(imap[i] == desired[i]);
    CHECK(imap.indexof(desired[i]) == i);
    CHECK(imap.key(i) == desired[i]);
  }
}
