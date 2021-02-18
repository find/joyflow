#include <doctest/doctest.h>

#include <core/oparg.h>

TEST_CASE("OP.Arg")
{
  using namespace joyflow;
  ArgDesc desc = ArgDescBuilder("test")
                     .type(ArgType::INT)
                     .description("test")
                     .defaultExpression(0, "1023")
                     .valueRange(0, 101)
                     .closeRange(true, true);

  ArgValue v(&desc, nullptr);
  CHECK(v.asBool() == true);
  CHECK(v.asInt() == 101);
  CHECK(v.asReal() == 101.0);
  CHECK(v.asString() == "101");
  CHECK(v.getRawExpr() == "1023");

  desc.closeRange[0] = desc.closeRange[1] = false;
  v.setRawExpr("1`string.rep('0',8)``24/12`");
  v.eval(nullptr);
  CHECK(v.asString() == "1000000002");
  CHECK(v.asInt() == 1000000002);
  CHECK(v.asReal() == 1000000002);

  desc.closeRange[0] = desc.closeRange[1] = true;
  v.eval(nullptr);
  CHECK(v.asBool() == true);
  CHECK(v.asInt() == 101);
  CHECK(v.asReal() == 101.0);
  CHECK(v.asString() == "101");
}
