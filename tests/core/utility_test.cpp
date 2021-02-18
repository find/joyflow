#include <doctest/doctest.h>
#include <core/vector.h>
#include <core/utility.h>

using namespace joyflow;

TEST_CASE("UtilityFunctions")
{
  auto v = toReal("10.24");
  CHECK(v == 10.24);
  CHECK(toReal("10.24") == 10.24);
  CHECK(toInt("1212142") == 1212142);
  CHECK(increaseNumericSuffix("") == "1");
  CHECK(increaseNumericSuffix("0") == "1");
  CHECK(increaseNumericSuffix("9") == "10");
  CHECK(increaseNumericSuffix("hello") == "hello1");
  CHECK(increaseNumericSuffix("hello001") == "hello002");
  CHECK(increaseNumericSuffix("hello009") == "hello010");
  CHECK(increaseNumericSuffix("h100000000000000000000000000000000000000") ==
        "h100000000000000000000000000000000000001");
  CHECK(increaseNumericSuffix("h999999999999999999999999999999999999999") ==
        "h1000000000000000000000000000000000000000");
  CHECK(increaseNumericSuffix("h12a0") == "h12a1");
  CHECK(increaseNumericSuffix("h12a0a") == "h12a0a1");

  Vector<int> vi = {0, 1};
  ensureVectorSize(vi, 4);
  ensureVectorSize(vi, 7, -1);
  CHECK(vi.size() == 7);
  CHECK(vi[0] == 0);
  CHECK(vi[1] == 1);
  CHECK(vi[2] == 0);
  CHECK(vi[3] == 0);
  CHECK(vi[4] == -1);
  CHECK(vi[5] == -1);
  CHECK(vi[6] == -1);

  Vector<int>    unordered      = {5, 2, 1, 3, 0, 10086, 12};
  Vector<size_t> expected_order = {4, 2, 1, 3, 0, 6, 5};
  Vector<size_t> order;
  argsort(order, unordered);
  CHECK(0 == std::memcmp(order.data(), expected_order.data(), sizeof(size_t) * order.size()));
  Vector<int> expected_sorted = {0, 1, 2, 3, 5, 12, 10086};
  Vector<int> sorted;
  sorted.resize(unordered.size());
  std::transform(order.begin(), order.end(), sorted.begin(), [&unordered](size_t idx) {
    return unordered[idx];
  });
  CHECK(0 == std::memcmp(sorted.data(), expected_sorted.data(), sizeof(int) * sorted.size()));
}
