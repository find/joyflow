#include "core/traits.h"
#include <doctest/doctest.h>
#include <core/datatable.h>
#include <core/luabinding.h>

#include <glm/glm.hpp>
#include <glm/gtx/io.hpp>
#include <mimalloc.h>
#include <sol/sol.hpp>

#include <cstdio>
#include <cstring>

namespace joyflow {
  doctest::String toString(CellIndex idx) {
    return doctest::toString(idx.value());
  }
}

TEST_CASE("DataTable.NumericDataInterface")
{
  using namespace joyflow;
  auto            pcollection = newDataCollection();
  DataCollection& collection  = *pcollection;
  collection.addTable();
  auto* column = collection.getTable(0)->createColumn("position", vec3(3, 2, 1));
  CHECK(column);
  CHECK(column->refcnt() == 1);
  CHECK(column->asNumericData());
  // link error -----------------------------------+
  //   (clang bug?)                                |
  // CHECK(column->dataType() == TypeInfo<real>::dataType);
  CHECK(column->dataType() == DataType::DOUBLE);

  collection.addRows(0,2);
  collection.set<vec3>(0, "position", 1, vec3(1, 2, 3));

  // data collection interface
  auto p = collection.get<vec3>(0, "position", 1);
  CHECK(p == vec3(1, 2, 3));
  float  arr[6];
  size_t len;
  column->asNumericData()->getFloatArray(arr, len, 0);
  CHECK(len == 6);
  float const expected[] = {3, 2, 1, 1, 2, 3};
  CHECK(std::memcmp(arr, expected, sizeof(expected)) == 0);
  auto iv = collection.get<ivec3>(0, "position", 1); // check type conversion
  CHECK(iv == ivec3(1, 2, 3));
  CHECK(collection.get<uint32_t>(0, "position", 1, 1) == 2); // check element-wise accessor

  // column interface
  CHECK(column->get<float>(CellIndex(1), 2) == 3);
  CHECK(column->get<vec3>(CellIndex(1)) == p);
  CHECK(column->get<vec3>(CellIndex(1024)) == vec3(3, 2, 1)); // out-of-bound returns default value
  CHECK(column->get<int>(CellIndex(91), 0) == 3);             // out-of-bound returns default value
  CHECK(column->get<int>(CellIndex(2), 1) == 2);              // out-of-bound returns default value
  CHECK(column->get<int>(CellIndex(20), 2) == 1);             // out-of-bound returns default value

  column->reserve(64);
  column->set<int>(CellIndex(23), 1, 1);
  CHECK(column->get<vec3>(CellIndex(23)) == vec3(3, 1, 1));
  CHECK(column->get<vec3>(CellIndex(22)) == vec3(3, 2, 1));
  CHECK(column->get<glm::vec<2, float>>(CellIndex(1)) == glm::vec<2, float>(1, 2));
  CHECK(column->get<int>(CellIndex(23)) == 3);

  // wrong tuple size throws error (?)
  // CHECK_THROWS(column->get<vec4>(CellIndex(1)));
  // CHECK_THROWS(column->get<glm::qua<float>>(CellIndex(1)));
}

typedef struct
{
  glm::ivec4 bones;
  glm::vec4  weights;
} BoneWeights;
std::ostream& operator<<(std::ostream& os, BoneWeights const& bw)
{
  return os << bw.bones << " : " << bw.weights;
}
bool operator==(BoneWeights const& a, BoneWeights const& b)
{
  return std::memcmp(&a, &b, sizeof(a)) == 0;
}
TEST_CASE("DataTable.FixSizedDataInterface")
{
  using namespace joyflow;
  auto            pcollection = newDataCollection();
  DataCollection& collection  = *pcollection;
  collection.addTable();
  auto* column = collection.getTable(0)->createColumn("weights", BoneWeights{{0, 0, 0, 0}, {1, 0, 0, 0}});
  CHECK(column);
  CHECK(column->asFixSizedData());
  CHECK(column->get<BoneWeights>(CellIndex(101101)) == BoneWeights{{0, 0, 0, 0}, {1, 0, 0, 0}});

  BoneWeights x = {{1, 2, 3, 4}, {.25, .5, .25, 0}};
  column->set<BoneWeights>(CellIndex(12), x);
  CHECK_THROWS(column->get<vec4>(CellIndex(1)));
  CHECK(column->get<BoneWeights>(CellIndex(12)) == x);
  CHECK(column->get<BoneWeights>(CellIndex(7)) == BoneWeights{{0, 0, 0, 0}, {1, 0, 0, 0}});
}

TEST_CASE("DataTable.BlobInterface")
{
  using namespace joyflow;
  {
    auto            pcollection = newDataCollection();
    DataCollection& collection  = *pcollection;
    collection.addTable();
    auto* column = collection.getTable(0)->createColumn<String>("name");
    CHECK(column);
    CHECK(!column->asBlobData()->getBlob(CellIndex(0)));
    column->set(CellIndex(0), new SharedBlob("hello world", 11));
    column->set<String>(CellIndex(4), "hello world");
    CHECK(column->get<String>(CellIndex(4)) == "hello world");
    CHECK_THROWS(column->get<vec4>(CellIndex(1)));
    column->set<String>(CellIndex(1), "hello world");
    CHECK(column->asBlobData()->getBlob(CellIndex(1)) == column->asBlobData()->getBlob(CellIndex(4)));
    auto helloworld = column->asBlobData()->getBlob(CellIndex(1));
    column->set<String>(CellIndex(1), "whatever");
    CHECK(helloworld == column->asBlobData()->getBlob(CellIndex(4)));
    CHECK(column->asBlobData()->getBlob(CellIndex(1)) != column->asBlobData()->getBlob(CellIndex(4)));
    CHECK(column->asBlobData()->getBlob(CellIndex(1))->refcnt() == 3);
    column->asBlobData()->setBlob(CellIndex(2), new SharedBlob("whatever", 8));
    CHECK(column->asBlobData()->getBlob(CellIndex(1))->refcnt() == 4);
    CHECK(column->get<String>(CellIndex(1)) == column->get<String>(CellIndex(2)));

    auto share = column->share();
    auto clone = column->clone();
    CHECK(column->get<String>(CellIndex(1)) == share->get<String>(CellIndex(2)));
    CHECK(share->asBlobData()->getBlob(CellIndex(1)) == clone->asBlobData()->getBlob(CellIndex(1)));
    CHECK(share->toString(CellIndex(1)) == "whatever");
  }
}

TEST_CASE("DataTable.Sharing")
{
  using namespace joyflow;
  auto            pcollection = newDataCollection();
  DataCollection& collection  = *pcollection;
  collection.addTable();
  auto* column = collection.getTable(0)->createColumn("position", vec3(0, 0, 0));
  CHECK(column);

  auto cp = column->share();
  CHECK(cp);
  CHECK(cp->refcnt() == 1);
  CHECK(cp->isUnique() == false);
  CHECK(cp->get<vec3>(CellIndex(2)) == vec3(0, 0, 0));
  CHECK(cp->isUnique() == false);
  CHECK(column->isUnique() == false);

  CHECK_THROWS(cp->set<vec3>(CellIndex(6), vec3(3, 1, 4)));
  cp->makeUnique();
  cp->reserve(8);
  cp->set<vec3>(CellIndex(6), vec3(3, 1, 4));
  CHECK(cp->isUnique() == true);
  CHECK(column->isUnique() == true);
  CHECK(cp->get<vec3>(CellIndex(6)) == vec3(3, 1, 4));
  CHECK(column->get<vec3>(CellIndex(6)) == vec3(0, 0, 0));

  auto* weights = collection.getTable(0)->createColumn("weights", BoneWeights{});
  CHECK(weights);
  CHECK(weights->isUnique());

  auto wcp = weights->share();
  CHECK(weights->isUnique() == false);
  CHECK_THROWS(weights->set<BoneWeights>(CellIndex(7), BoneWeights{{1, 2, 3, 4}, {5, 6, 7, 8}}));
  weights->makeUnique();
  CHECK(weights->isUnique());
  BoneWeights bw = {{1, 2, 3, 4}, {5, 6, 7, 8}};
  weights->set<BoneWeights>(CellIndex(7), bw);
  CHECK(wcp->get<BoneWeights>(CellIndex(7)) == BoneWeights{});
  CHECK(weights->get<BoneWeights>(CellIndex(7)) == bw);
}

TEST_CASE("DataTable.Defragment")
{
  using namespace joyflow;
  auto            pcollection = newDataCollection();
  DataCollection& collection  = *pcollection;
  collection.addTable();
  auto* column = collection.getTable(0)->createColumn( "position", vec3(0, 0, 0));
  CHECK(column);

  collection.addRow(0);
  collection.addRow(0);
  collection.set<vec3>(0, "position", 1, vec3(1, 2, 3));
  CHECK(collection.get<vec3>(0, "position", 0) == vec3(0, 0, 0));
  CHECK(collection.get<vec3>(0, "position", 1) == vec3(1, 2, 3));

  CHECK(collection.getIndex(0, 0) == CellIndex(0));
  collection.removeRow(0, 0);
  CHECK(collection.getRow(0, CellIndex(0)) == -1);
  CHECK(collection.get<vec3>(0, "position", 0) == vec3(1, 2, 3));
  CHECK(column->get<vec3>(CellIndex(0)) == vec3(0, 0, 0));

  CHECK(collection.getIndex(0, 0) == CellIndex(1));
  CHECK(collection.numRows(0)==1);
  CHECK(collection.numIndices(0)==2);
  collection.defragment();
  CHECK(collection.numRows(0)==1);
  CHECK(collection.numIndices(0)==1);
  CHECK(collection.getIndex(0, 0) == CellIndex(0));
  CHECK(collection.get<vec3>(0, "position", 0) == vec3(1, 2, 3));
  CHECK(column->get<vec3>(CellIndex(0)) == vec3(1, 2, 3));

  auto newidx = collection.addRows(0, 100);
  collection.removeRows(0, 11, 80);
  CHECK(collection.numRows(0)==21);
  CHECK(collection.numIndices(0)==101);
  CHECK(collection.getRow(0,newidx) == 1);
  CHECK(collection.getRow(0,newidx+9) == 10);
  CHECK(collection.getRow(0,newidx+10) == -1);
  CHECK(collection.getRow(0,newidx+89) == -1);
  CHECK(collection.getRow(0,newidx+90) == 11);
  collection.defragment();
  CHECK(collection.numRows(0)==21);
  CHECK(collection.numIndices(0)==21);
  CHECK(collection.getRow(0,newidx) == 1);
  CHECK(collection.getRow(0,newidx+10) == 11);
  CHECK(collection.getIndex(0,20) == CellIndex(20));
  CHECK(collection.getIndex(0,21) == CellIndex(-1));
  CHECK(collection.getRow(0,newidx+100) == -1);
}

TEST_CASE("DataTable.Container")
{
  using namespace joyflow;
  auto pcollection = newDataCollection();
  pcollection->addTable();
  auto* column = pcollection->getTable(0)->createColumn<Vector<float>>("weights");
  auto* ci     = column->asVectorData();
  CHECK(ci);

  // shouldn't compile:
  // pcollection->getTable(0)->createColumn<Vector<BoneWeights>>("vector_of_weights");
  
  CHECK_THROWS(ci->asVector<float>(CellIndex(10)));
  column->reserve(24);
  auto* vf = ci->asVector<float>(CellIndex(10));
  CHECK(vf);
  vf->push_back(10086);
  vf->push_back(1024);

  CHECK((*vf)[0] == 10086);
  CHECK((*vf)[1] == 1024);

  CHECK(ci->asVector<int>(CellIndex(8)) == nullptr);
  CHECK(ci->asVector<glm::vec3>(CellIndex(8)) == nullptr);
  vf = ci->asVector<float>(CellIndex(7));
  CHECK(vf);
  CHECK(vf->empty());

  {
    auto* vv = pcollection->getTable(0)->createColumn<Vector<vec3>>("values");
    CHECK(vv);

    vv->reserve(40);
    auto* vvi  = vv->asVectorData();
    auto* elem = vvi->asVector<vec3>(CellIndex(10));
    CHECK(elem);
    CHECK(elem->empty());
    elem->push_back(vec3(1, 2, 4));
    CHECK(elem->pop_back() == vec3(1, 2, 4));
  }
}

TEST_CASE("DataTable.DataTable")
{
  using namespace joyflow;
  auto pcollection = newDataCollection();
  auto tbid = pcollection->addTable();
  auto* table = pcollection->getTable(tbid);
  auto* ci = table->createColumn<uint64_t>("population", 0);
  auto idx = table->addRows(1000);
  CHECK(idx.value() == 0);
  uint64_t val = 10243124;
  for (auto itr = idx, end = idx + 1000; itr < end; ++itr) {
    table->set<uint64_t>("population", itr, val++);
  }
  CHECK(table->get<uint64_t>("population", idx + 999, 0) == 10243124 + 999);
}

TEST_CASE("DataTable.LuaBinding")
{
  using namespace joyflow;
  {
    sol::state lua;
    bindLuaTypes(lua);

    auto pcollection = newDataCollection();
    pcollection->addTable();
    auto* table = pcollection->getTable(0);
    table->createColumn<real>("test",0.0);
    table->addRows(100);

    for (CellIndex c(0); c<100; ++c)
      table->set<real>("test", c, real(c.value()));

    lua["wd"] = table;
    CHECK(static_cast<real>(lua.safe_script("return wd:get('test', 0)")) == 0.0);
    CHECK(static_cast<real>(lua.safe_script("return wd:get('test', 3)")) == 3.0);
    CHECK(static_cast<real>(lua.safe_script("return wd:get('test', 7)")) == 7.0);
    lua.safe_script("wd:set('test', 11, 1021, 0)");
    CHECK(table->get<real>("test", 11, 0) == 1021.0);

    auto* scol = table->createColumn<String>("name");
    scol->set<String>(CellIndex(11),"foobar");
    table->set<String>("name", 12, "foobar");
    table->set<String>("name", 15, "foobar");
    table->set<String>("name", 13, "whatever");
    table->set<String>("name", 42, "helloworld");
    CHECK(static_cast<bool>(lua.safe_script("return wd:get('name', 0) == ''")) == true);
    CHECK(lua.safe_script("return (function(x) return wd:get('name', 11) .. x end)('foobar')").operator std::string() == "foobarfoobar");
    CHECK(lua.safe_script("return wd:get('name', 13)").operator std::string() == "whatever");
    CHECK(lua.safe_script("return wd:get('name', 42)").operator std::string() == "helloworld");
    CHECK(static_cast<bool>(lua.safe_script("return wd:get('name', 43) == ''")) == true);
    CHECK(static_cast<bool>(lua.safe_script("return wd:get('non-exists', 43) == nil")) == true);

    table->createColumn("pos", vec3(0,1,2));
    table->set("pos", 21, vec3(5,4,3));
    lua.safe_script("a,b,c = wd:get('pos', 42)");
    CHECK(double(lua["a"]) == 0);
    CHECK(double(lua["b"]) == 1);
    CHECK(double(lua["c"]) == 2);
    lua.safe_script("a,b,c = wd:get('pos', 21)");
    CHECK(double(lua["a"]) == 5);
    CHECK(double(lua["b"]) == 4);
    CHECK(double(lua["c"]) == 3);
    lua.safe_script("wd:set('pos', 21, 42, 1)");
    CHECK(table->get<int>("pos", 21, 1) == 42);

    CHECK_THROWS(lua.safe_script("wd.get('hello', 0)", sol::script_throw_on_error)); // should use wd:get
    CHECK_THROWS(lua.safe_script("wd:get(0)", sol::script_throw_on_error));          // wrong argument type

    lua["dc"] = pcollection.get();
    CHECK(lua.safe_script("return dc:table(0):get('name',11) .. wd:get('name',13)").operator std::string() == "foobarwhatever");
  }
  CHECK(Stats::livingCount() == 0);
}

TEST_CASE("DataTable.LuaReadonly")
{
  using namespace joyflow;
  sol::state lua;
  bindLuaTypes(lua, true);
  auto pcollection = newDataCollection();
  pcollection->addTable();
  auto* table = pcollection->getTable(0);
  table->createColumn<real>("test",0.0);
  table->addRows(100);

  for (CellIndex c(0); c<100; ++c)
    table->set<real>("test", c, real(c.value()));

  lua["wd"] = table;
  CHECK(static_cast<real>(lua.safe_script("return wd:get('test', 0)")) == 0.0);
  CHECK(static_cast<real>(lua.safe_script("return wd:get('test', 3)")) == 3.0);
  CHECK(static_cast<real>(lua.safe_script("return wd:get('test', 7)")) == 7.0);
  CHECK_THROWS(lua.safe_script("wd:set('test', 11, 1021, 0)", sol::script_throw_on_error));
}

TEST_CASE("DataTable.join.numeric")
{
  using namespace joyflow;
  {
    auto dc1 = joyflow::newDataCollection();
    auto dc2 = joyflow::newDataCollection();

    dc1->addTable();
    dc2->addTable();

    auto *t1 = dc1->getTable(0);
    t1->addRows(11);
    auto c1 = t1->createColumn<int>("iii", 1024);
    auto c2 = t1->createColumn<vec3>("pos", vec3(3,1,4));

    auto *t2 = dc2->getTable(0);
    t2->addRows(31);
    auto c3 = t2->createColumn<int>("iii", 12);
    auto c4 = t2->createColumn<vec3>("normal", vec3(0,1,0));

    t1->set<int>("iii", 0, 0);
    t1->set<int>("iii", 2, 1);
    t1->set<int>("iii", 4, 2);
    t1->set<vec3>("pos", 1, vec3(0,0,0));
    t1->set<vec3>("pos", 3, vec3(0,0,1));
    t1->set<vec3>("pos", 5, vec3(0,0,2));

    t2->set<int>("iii", 0, 5);
    t2->set<int>("iii", 3, 7);
    t2->set<int>("iii", 8, 9);
    t2->set<vec3>("normal", 11, vec3(1,0,0));
    t2->set<vec3>("normal", 13, vec3(0,0,1));
    t2->set<vec3>("normal", 15, vec3(0,0,-1));

    dc1->join(dc2.get());

    CHECK(dc1->getTable(0)->numRows() == 42);
    CHECK(dc1->get<int>(0, "iii", 0, 0) == 0);
    CHECK(dc1->get<int>(0, "iii", 2, 0) == 1);
    CHECK(dc1->get<int>(0, "iii", 4, 0) == 2);
    CHECK(dc1->get<int>(0, "iii", 5, 0) == 1024);
    CHECK(dc1->get<int>(0, "iii", 11, 0) == 5);
    CHECK(dc1->get<int>(0, "iii", 12, 0) == 12);
    CHECK(dc1->get<int>(0, "iii", 13) == 12);
    CHECK(dc1->get<int>(0, "iii", 14) == 7);
    CHECK(dc1->get<int>(0, "iii", 15) == 12);
    CHECK(dc1->get<int>(0, "iii", 19) == 9);
    CHECK(dc1->get<int>(0, "iii", 40) == 12);

    CHECK(dc1->get<vec3>(0, "pos", 0) == vec3(3,1,4));
    CHECK(dc1->get<vec3>(0, "pos", 1) == vec3(0,0,0));
    CHECK(dc1->get<vec3>(0, "pos", 3) == vec3(0,0,1));
    CHECK(dc1->get<vec3>(0, "pos", 30) == vec3(3,1,4));

    CHECK(dc1->get<vec3>(0, "normal", 0) == vec3(0,1,0));
    CHECK(dc1->get<vec3>(0, "normal", 11) == vec3(0,1,0));
    CHECK(dc1->get<vec3>(0, "normal", 13) == vec3(0,1,0));
    CHECK(dc1->get<vec3>(0, "normal", 22) == vec3(1,0,0));
    CHECK(dc1->get<vec3>(0, "normal", 24) == vec3(0,0,1));
    CHECK(dc1->get<vec3>(0, "normal", 26) == vec3(0,0,-1));
    CHECK(dc1->get<vec3>(0, "normal", 30) == vec3(0,1,0));
    CHECK(dc1->get<vec3>(0, "normal", 41) == vec3(0,1,0));
  }
  CHECK(Stats::livingCount() == 0);
}

struct Test
{
  int x,y;
};
bool operator==(Test const& a, Test const& b)
{
  return a.x==b.x && a.y==b.y;
}
std::ostream& operator<<(std::ostream& os, Test const& t)
{
  return os << t.x << ", " << t.y;
}

TEST_CASE("DataTable.join.structureddata")
{
  using namespace joyflow;
  {
    auto dc1 = joyflow::newDataCollection();
    auto dc2 = joyflow::newDataCollection();

    dc1->addTable();
    dc2->addTable();

    auto *t1 = dc1->getTable(0);
    t1->addRows(11);
    auto c1 = t1->createColumn<String>("name");

    BoneWeights const defaultWeights = {{0,0,0,0}, {1,0,0,0}};
    

    auto c2 = t1->createColumn<BoneWeights>("weights", defaultWeights);

    auto *t2 = dc2->getTable(0);
    t2->addRows(31);
    auto c3 = t2->createColumn<String>("name");
    auto c4 = t2->createColumn<String>("note");
    auto c5 = t2->createColumn<Test>("test", Test{4,2});

    t1->set<StringView>("name", 0, "rope0");
    t1->set<StringView>("name", 2, "rope2");
    t1->set<StringView>("name", 4, "rope4");
    t1->set<StringView>("name", 6, "rope6");

    t1->set<BoneWeights>("weights", 0, BoneWeights{{0,1,2,3}, {1,0,0,0}});
    t1->set<BoneWeights>("weights", 2, BoneWeights{{0,1,2,3}, {1,0,0,0}});
    t1->set<BoneWeights>("weights", 4, BoneWeights{{0,1,2,3}, {1,0,0,0}});
    t1->set<BoneWeights>("weights", 6, BoneWeights{{0,1,2,3}, {1,0,0,0}});

    t2->set<String>("name", 0, "ropex");
    t2->set<String>("name", 2, "ropey");
    t2->set<String>("name", 4, "ropez");
    t2->set<String>("name", 6, "ropew");

    t2->set<String>("note", 1, "blah blah");

    t2->set<Test>("test", 0, Test{1,2});
    t2->set<Test>("test", 1, Test{2,3});
    t2->set<Test>("test", 2, Test{1,4});
    t2->set<Test>("test", 4, Test{3,7});
    t2->set<Test>("test", 8, Test{5,3});

    dc1->join(dc2.get());

    CHECK(dc1->getTable(0)->numRows() == 42);
    CHECK(dc1->get<StringView>(0, "name", 0) == "rope0");
    CHECK(dc1->get<StringView>(0, "name", 1) == "");
    CHECK(dc1->get<StringView>(0, "name", 2) == "rope2");
    CHECK(dc1->get<StringView>(0, "name", 3) == "");
    CHECK(dc1->get<StringView>(0, "name", 4) == "rope4");
    CHECK(dc1->get<StringView>(0, "name", 5) == "");
    CHECK(dc1->get<StringView>(0, "name", 6) == "rope6");
    CHECK(dc1->get<StringView>(0, "name", 7) == "");
    CHECK(dc1->get<StringView>(0, "name", 10) == "");
    CHECK(dc1->get<StringView>(0, "name", 11) == "ropex");
    CHECK(dc1->get<StringView>(0, "name", 12) == "");
    CHECK(dc1->get<StringView>(0, "name", 13) == "ropey");
    CHECK(dc1->get<StringView>(0, "name", 15) == "ropez");
    CHECK(dc1->get<StringView>(0, "name", 17) == "ropew");
    CHECK(dc1->get<StringView>(0, "name", 18) == "");

    CHECK(dc1->get<StringView>(0, "note", 0) == "");
    CHECK(dc1->get<StringView>(0, "note", 10) == "");
    CHECK(dc1->get<StringView>(0, "note", 12) == "blah blah");
    CHECK(dc1->get<StringView>(0, "note", 13) == "");

    CHECK(dc1->get<BoneWeights>(0, "weights", 0) == BoneWeights{{0,1,2,3}, {1,0,0,0}});
    CHECK(dc1->get<BoneWeights>(0, "weights", 1) == defaultWeights);
    CHECK(dc1->get<BoneWeights>(0, "weights", 2) == BoneWeights{{0,1,2,3}, {1,0,0,0}});
    CHECK(dc1->get<BoneWeights>(0, "weights", 3) == defaultWeights);
    CHECK(dc1->get<BoneWeights>(0, "weights", 4) == BoneWeights{{0,1,2,3}, {1,0,0,0}});
    CHECK(dc1->get<BoneWeights>(0, "weights", 5) == defaultWeights);
    CHECK(dc1->get<BoneWeights>(0, "weights", 6) == BoneWeights{{0,1,2,3}, {1,0,0,0}});
    CHECK(dc1->get<BoneWeights>(0, "weights", 7) == defaultWeights);
    CHECK(dc1->get<BoneWeights>(0, "weights", 8) == defaultWeights);
    CHECK(dc1->get<BoneWeights>(0, "weights", 13) == defaultWeights);
    CHECK(dc1->get<BoneWeights>(0, "weights", 31) == defaultWeights);

    CHECK_THROWS(dc1->get<BoneWeights>(0, "test", 31));
    CHECK(dc1->get<Test>(0, "test", 0)  == Test{4,2});
    CHECK(dc1->get<Test>(0, "test", 1)  == Test{4,2});
    CHECK(dc1->get<Test>(0, "test", 2)  == Test{4,2});
    CHECK(dc1->get<Test>(0, "test", 4)  == Test{4,2});
    CHECK(dc1->get<Test>(0, "test", 8)  == Test{4,2});
    CHECK(dc1->get<Test>(0, "test", 11) == Test{1,2});
    CHECK(dc1->get<Test>(0, "test", 12) == Test{2,3});
    CHECK(dc1->get<Test>(0, "test", 13) == Test{1,4});
    CHECK(dc1->get<Test>(0, "test", 14) == Test{4,2});
    CHECK(dc1->get<Test>(0, "test", 15) == Test{3,7});
    CHECK(dc1->get<Test>(0, "test", 16) == Test{4,2});
    CHECK(dc1->get<Test>(0, "test", 19) == Test{5,3});
    CHECK(dc1->get<Test>(0, "test", 20) == Test{4,2});
    CHECK(dc1->get<Test>(0, "test", 40) == Test{4,2});
    CHECK(dc1->get<Test>(0, "test", 41) == Test{4,2});
  }
  CHECK(Stats::livingCount() == 0);
  mi_collect(1);
  mi_stats_print(nullptr);
}

TEST_CASE("Datatable.Sort")
{
  using namespace joyflow;
  auto dc = newDataCollection();
  dc->addTable();
  auto* tb = dc->getTable(0);
  auto* col = tb->createColumn<int>("i",0);
  for (int i=0;i<10;++i) {
    tb->addRow();
    tb->set<int>("i", i, i);
  }
  Vector<sint> order = {3,1,2,4,6,5,7,8,9,0};
  tb->sort(order);
  for(sint i=0, n=order.ssize(); i<n; ++i) {
    CHECK(tb->get<int>("i",i) == order[i]);
    CHECK(tb->getRow(CellIndex(order[i])) == i);
  }
}

