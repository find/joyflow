#include <doctest/doctest.h>
#include <core/vector.h>
#include <algorithm>
#include <iterator>

class CleanString : public std::string {
public:
  using Base = std::string;
  CleanString():Base() {}
  CleanString(Base&& s):Base(s) {}
  CleanString(CleanString const& s):Base(s) {}
  CleanString(const char* s):Base(s) {}

  ~CleanString() { assign(""); }
};
doctest::String toString(CleanString const& cs) { return cs.c_str(); }

TEST_CASE("Vector")
{
  using joyflow::Vector;
  Vector<int> v1;
  int         ncases = 0xffff;
  for (int i = 0; i < ncases; ++i)
    v1.push_back(i);
  auto v2 = v1;
  CHECK(v1.size() == ncases);
  CHECK(v2.size() == v1.size());
  CHECK(v2[1023] == v1[1023]);
  v1.insert(v1.begin() + 10, 1);
  CHECK(v1[10] == 1);
  CHECK(v1[11] == 10);
  CHECK(v1.size() - v2.size() == 1);
  CHECK(v1.pop_back() == ncases - 1);
  std::swap(v1, v2);
  CHECK(v2[10] == 1);
  v2.resize(13);
  v2.erase(v2.begin() + 3);
  v2.erase(std::find(v2.begin(), v2.end(), 7));
  v1 = {0, 1, 2, 4, 5, 6, 8, 9, 1, 10, 11};
  CHECK(memcmp(v1.data(), v2.data(), v1.size() * sizeof(int)) == 0);
  v1.push_back(10086);
  v2.insert(v2.end(), 10086);
  CHECK(memcmp(v1.data(), v2.data(), v1.size() * sizeof(int)) == 0);
  Vector<CleanString> vs;
  for (int i = 0; i < ncases; ++i)
    vs.push_back(std::to_string(i * 2));
  vs.resize(16);
  CHECK(vs.capacity() >= ncases);
  CHECK(vs.size() == 16);
  CHECK(vs[1241] == "");
  CHECK(vs.back() == "30");
  CHECK(*vs.insert(vs.begin(), "hello!") == "hello!");
  CHECK(vs.size() == 17);
  vs.shrink_to_fit();
  CHECK(vs.capacity() == 17);
  CHECK(vs.pop_back() == "30");
  std::transform(
      vs.begin(), vs.end(), vs.begin(), [](std::string const& x) { return ">>>" + x + "<<<"; });
  CHECK(vs.pop_back() == ">>>28<<<");
  Vector<std::string> vss;
  std::transform(vs.begin(), vs.end(), std::back_inserter(vss), [](std::string const& x) {
    return "<<<" + x + ">>>";
  });
  CHECK(vss.size() == vs.size());
  CHECK(vss[0] == "<<<>>>hello!<<<>>>");
  CHECK(vss[6] == "<<<>>>10<<<>>>");

  static size_t objcnt = 0;
  struct alignas(8) SomeObj
  {
    int  payload = 1234;
    char padding[3];
    SomeObj() { ++objcnt; }
    SomeObj(SomeObj const&) { ++objcnt; }
    ~SomeObj() { --objcnt; }
  };
  {
    Vector<SomeObj> vo;
    vo.reserve(1024);
    CHECK(objcnt == 0);
    for (int i = 0; i < ncases; ++i) {
      vo.emplace_back();
    }
    CHECK((reinterpret_cast<ptrdiff_t>(&vo[103]) & 7) == 0);
    CHECK(objcnt == ncases);

    for (int i = 0; i < 13; ++i)
      vo.pop_back();
    CHECK(objcnt == ncases - 13);

    Vector<SomeObj> another = std::move(vo);
    CHECK(objcnt == ncases - 13);

    Vector<SomeObj> cp = another;
    CHECK(objcnt == (ncases - 13) * 2);
  }
  CHECK(objcnt == 0);
}
