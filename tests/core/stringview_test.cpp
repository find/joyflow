#include <doctest/doctest.h>
#include <core/stringview.h>
#include <iostream>

TEST_CASE("StringView")
{
  using joyflow::String;
  using joyflow::StringView;
  CHECK(StringView("abc") == String("abc"));
  CHECK(StringView("helloworld").substr(0, 5) == "hello");
  CHECK(StringView("helloworld").substr(5) == "world");
  CHECK(StringView("12431") < "124310");
  CHECK(StringView("12431") < "12432");
}
