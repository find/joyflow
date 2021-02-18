#include <doctest/doctest.h>

#include <core/opgraph.h>
#include <core/opkernel.h>
#include <core/opdesc.h>
#include <core/datatable.h>
#include <core/opcontext.h>
#include <core/opbuiltin.h>
#include <core/stats.h>
#include <core/oparg.h>
#include <core/oplib.h>
#include <glm/glm.hpp>

#include <nlohmann/json.hpp>
#include <fstream>

TEST_CASE("OpGrpah.Eval")
{
  using namespace joyflow;
  static int init_cnt = 0;
  static int add_cnt  = 0;
  static int sum_cnt  = 0;
  class InitOp : public OpKernel
  {
  public:
    virtual void eval(OpContext& context) const override
    {
      ++init_cnt;
      DataCollection* output0 = context.reallocOutput(0);
      output0->addTable();
      auto* column = output0->getTable(0)->createColumn("Position", vec3());
      auto* scolumn = output0->getTable(0)->createColumn<String>("name");
      int   i      = 0;
      auto cnt = context.arg("count").asInt();
      auto stt = context.arg("start_idx").asInt();
      for (auto index = output0->addRows(0, cnt); index < cnt; ++index) {
        column->set(index, vec3(0, stt+i++, 0));
        scolumn->set(index, fmt::format("item{}",stt+index.value()));
      }
    }
    static OpDesc mkDesc()
    {
      return makeOpDesc<InitOp>("init")
        .numRequiredInput(0)
        .argDescs({
            ArgDescBuilder("count").type(ArgType::INT).defaultExpression(0, "1024"),
            ArgDescBuilder("start_idx").type(ArgType::INT).defaultExpression(0, "0")
        });
    }
  };
  class AddOp : public OpKernel
  {
  public:
    virtual void eval(OpContext& context) const override
    {
      ++add_cnt;
      if (context.getNumInputs() > 0) {
        DataCollection* input0  = context.fetchInputData(0);
        DataCollection* output0 = context.copyInputToOutput(0, 0);
        output0->getTable(0)->makeUnique();
        auto*           icolumn = input0->getColumn(0, "Position");
        auto*           ocolumn = output0->getColumn(0, "Position");
        CHECK_THROWS(ocolumn->set<vec3>(
            CellIndex(512), vec3(1, 2, 3))); // write before makeUnique should cause error
        ocolumn->makeUnique();
        for (CellIndex idx(0); idx < input0->numIndices(0); ++idx) {
          auto v = icolumn->get<vec3>(idx);
          ocolumn->set<vec3>(idx, v + context.arg("amount").asReal3());
        }
      }
    }
    static OpDesc mkDesc()
    {
      return makeOpDesc<AddOp>("add")
          .argDescs({{ArgType::REAL, "amount", "Amount", 3, "", {"1024", "0", "0"}}})
          .get();
    }
  };
  class SumOp : public OpKernel
  {
  public:
    virtual void eval(OpContext& context) const override
    {
      ++sum_cnt;
      CHECK(context.getNumInputs() == 2);
      context.requireInput(0);
      DataCollection* o0      = context.copyInputToOutput(0, 0);
      o0->getTable(0)->makeUnique();
      auto*           ocolumn = o0->getColumn(0, "Position");
      ocolumn->makeUnique();
      auto* i0 = context.fetchInputData(1);
      CHECK(i0);
      auto* icolumn = i0->getColumn(0, "Position");
      for (CellIndex idx(0); idx < i0->numIndices(0); ++idx) {
        auto a = ocolumn->get<vec3>(idx);
        auto b = icolumn->get<vec3>(idx);
        ocolumn->set<vec3>(idx, a + b);
      }
    }
    static OpDesc mkDesc() { return makeOpDesc<SumOp>("sum").numRequiredInput(2).get(); }
  };
  class Noop : public OpKernel
  {
  public:
    virtual void eval(OpContext& io) const override
    {
      if (io.getNumInputs() == 1)
        io.copyInputToOutput(0);
    }
    static OpDesc mkDesc()
    {
      return makeOpDesc<Noop>("noop").numRequiredInput(1).numMaxInput(1).get();
    }
  };

  DOCTEST_SUBCASE("Eval")
  {
    OpRegistry::instance().add(InitOp::mkDesc());
    OpRegistry::instance().add(AddOp::mkDesc());
    OpRegistry::instance().add(SumOp::mkDesc());
    OpRegistry::instance().add(Noop::mkDesc());
    OpGraph* proot = newGraph("root");
    OpGraph& root  = *proot;
    String   init  = root.addNode("init", "Initialize");
    String   add   = root.addNode("add", "Add");
    String   sum   = root.addNode("sum", "Sum");
    String   noop  = root.addNode("noop", "NOOP");

    root.mutDesc().numOutputs = 2;
    root.setOutputNode(0, add);
    root.setOutputNode(1, noop);
    root.link(init, 0, add, 0);
    root.link(init, 0, sum, 0);
    root.link(add, 0, sum, 1);
    root.link(sum, 0, noop, 0);
    root.newContext();

    CHECK(root.evalNode(init)->get<vec3>(0, "Position", 42) == vec3(0, 42, 0));
    CHECK(init_cnt == 1);
    CHECK(add_cnt == 0);
    CHECK(sum_cnt == 0);

    CHECK(root.getOutput(0)->get<vec3>(0, "Position", 0) == vec3(1024, 0, 0));
    CHECK(root.getOutput(0)->get<vec3>(0, "Position", 512) == vec3(1024, 512, 0));
    CHECK(root.getOutput(0)->get<vec3>(0, "Position", 1023) == vec3(1024, 1023, 0));
    CHECK(init_cnt == 1);
    CHECK(add_cnt == 1);
    CHECK(sum_cnt == 0);

    // evaluator.eval(rootContext);
    // CHECK(init_cnt==1);
    // CHECK(add_cnt==1);
    // CHECK(sum_cnt==0);

    CHECK(root.getOutput(1)->get<vec3>(0, "Position", 512) == vec3(1024, 1024, 0));
    CHECK(root.getOutput(1)->get<vec3>(0, "Position", 513) == vec3(1024, 1026, 0));
    CHECK(init_cnt == 1);
    CHECK(add_cnt == 1);
    CHECK(sum_cnt == 1);

    root.node(add)->mutArg("amount").setRawExpr("42", 0);
    CHECK(root.getOutput(1)->get<vec3>(0, "Position", 512) == vec3(42, 1024, 0));
    CHECK(root.getOutput(1)->get<vec3>(0, "Position", 513) == vec3(42, 1026, 0));
    CHECK(init_cnt == 1);
    CHECK(add_cnt == 2);
    CHECK(sum_cnt == 2);

    root.removeNode("add");
    root.link(init, 0, sum, 1);
    CHECK(root.getOutput(1)->get<vec3>(0, "Position", 127) == vec3(0, 254, 0));
    CHECK(root.getOutput(1)->get<vec3>(0, "Position", 512) == vec3(0, 1024, 0));
    CHECK(init_cnt == 1);
    CHECK(add_cnt == 2);
    CHECK(sum_cnt == 3);

    Json json;
    CHECK(proot->save(json));
    std::ofstream jsfile("tests/intermediate/test-graph.json", std::ios::binary);
    auto jsstr = json.dump(2);
    CHECK(jsfile.write(jsstr.c_str(), jsstr.size()));
    deleteGraph(proot);
  }
  CHECK(Stats::livingCount() == 0);

  DOCTEST_SUBCASE("Deserialization")
  {
    OpGraph* proot = newGraph("root");
    std::ifstream jsfile("tests/intermediate/test-graph.json", std::ios::binary);
    Json json = Json::parse(jsfile);
    CHECK(proot->load(json));
    CHECK(proot->getOutput(1)->get<vec3>(0, "Position", 127) == vec3(0, 254, 0));
    CHECK(proot->getOutput(1)->get<vec3>(0, "Position", 512) == vec3(0, 1024, 0));

    registerBuiltinOps();
    String split = proot->addNode("split", "Split");
    proot->link("NOOP", 0, split, 0);
    auto* snode = proot->node(split);
    CHECK(snode!=nullptr);
    snode->mutArg("table").setInt(0);
    snode->mutArg("condition").setString("${Position.x}<500");
    auto splitTrue = proot->evalNode(split, 0);
    auto splitFalse = proot->evalNode(split, 1);

    CHECK(splitTrue);
    CHECK(splitFalse);
    for (sint row=0, n=splitTrue->numRows(0); row<n; ++row)
      CHECK(splitTrue->get<vec3>(0,"Position",row).x < 500);
    for (sint row=0, n=splitFalse->numRows(0); row<n; ++row)
      CHECK(splitFalse->get<vec3>(0,"Position",row).x >= 500);

    deleteGraph(proot);
  }
  CHECK(Stats::livingCount() == 0);
  // printf("-------------------------\n");
  // mi_collect(1);
  // mi_stats_print(nullptr);
}

TEST_CASE("OpGraph.Builtin")
{
  using namespace joyflow;
  {
    std::unique_ptr<OpGraph>   proot(newGraph("root"));

    auto init = proot->addNode("init", "init");
    auto split = proot->addNode("split", "split");
    auto sort = proot->addNode("sort", "sort");
    auto join = proot->addNode("join", "join");
    auto defrag = proot->addNode("defragment", "defrag");
    proot->node(init)->mutArg("count").setInt(10);
    proot->node(split)->mutArg("condition").setString("${Position.y}<5");
    proot->node(sort)->mutArg("key").setString("Position");
    proot->node(sort)->mutArg("order").setMenu(1);
    proot->link(init, 0, split, 0);
    proot->link(split, 0, join, 0);
    proot->link(split, 1, sort, 0);
    proot->link(sort, 0, join, 1);
    proot->link(join, 0, defrag, 0);
    auto newjoin = join;
    CHECK(proot->renameNode(join, "JJJ", newjoin));

    auto joinresult = proot->evalNode(newjoin);
    auto defragresult = proot->evalNode(defrag);

    int const expectedY[] = { 0,1,2,3,4, 9,8,7,6,5 };
    CHECK(joinresult->numRows(0) == 10);
    CHECK(joinresult->numRows(0) == defragresult->numRows(0));
    CHECK(defragresult->numRows(0) == defragresult->numIndices(0));
    for (sint i = 0, n = joinresult->numRows(0); i < n; ++i) {
      CHECK(joinresult->get<vec3>(0, "Position", i) == defragresult->get<vec3>(0, "Position", i));
      CHECK(joinresult->get<StringView>(0, "name", i) == defragresult->get<StringView>(0, "name", i));
      CHECK(defragresult->get<int>(0, "Position", i, 1) == expectedY[i]);
    }
    size_t sharedMem = 0, unsharedMem = 0;
    joinresult->countMemory(sharedMem, unsharedMem);
    spdlog::info("memory usage of {} : {} bytes shared, {} bytes unshared", newjoin, sharedMem, unsharedMem);
    defragresult->countMemory(sharedMem, unsharedMem);
    spdlog::info("memory usage of {} : {} bytes shared, {} bytes unshared", defrag, sharedMem, unsharedMem);

    // change start index and start over
    proot->node(init)->mutArg("start_idx").setInt(13);
    proot->node(split)->mutArg("condition").setString("${Position.y}<18");
    defragresult = proot->evalNode(defrag);
    StringView const expectedNames[] = {
      "item13", "item14", "item15", "item16", "item17",
      "item22", "item21", "item20", "item19", "item18"
    };
    for (sint i=0, n=defragresult->numRows(0); i<n; ++i) {
      CHECK(defragresult->get<StringView>(0, "name", i) == expectedNames[i]);
    }

    // another
    proot->node(init)->mutArg("start_idx").setInt(1013);
    proot->node(split)->mutArg("condition").setString("${Position.y}<1018");
    defragresult = proot->evalNode(defrag);
    joinresult = proot->evalNode(newjoin);
    StringView const newExpectedNames[] = {
      "item1013", "item1014", "item1015", "item1016", "item1017",
      "item1022", "item1021", "item1020", "item1019", "item1018"
    };
    for (sint i=0, n=defragresult->numRows(0); i<n; ++i) {
      CHECK(defragresult->get<StringView>(0, "name", i) == newExpectedNames[i]);
      CHECK(defragresult->get<StringView>(0, "name", i) == joinresult->get<StringView>(0, "name", i));
    }
    Stats::dumpLiving(stdout);
  }
  CHECK(Stats::livingCount() == 0);
}

TEST_CASE("OpGraph.FromUI")
{
  using namespace joyflow;
  {
    class CreateTestArray : public joyflow::OpKernel
    {
      void eval(joyflow::OpContext& ctx) const override
      {
        auto *dc = ctx.reallocOutput(0);
        auto tbid = dc->addTable();
        auto* tb = dc->getTable(tbid);
        int startIdx = int(ctx.arg("start_idx").asInt());
        tb->createColumn<int>("id",0);
        tb->createColumn<String>("name");
        tb->addRows(1024);
        int* idarr = tb->getColumn("id")->asNumericData()->rawBufferRW<int>(joyflow::CellIndex(0), 1024);
        for (int i = 0; i < 1024; ++i) {
          idarr[i] = i+startIdx;
          tb->set<joyflow::String>("name", i, "test" + std::to_string(i+startIdx));
        }
      }
      
    public:
      static joyflow::OpDesc mkDesc()
      {
        using namespace joyflow;
        return makeOpDesc<CreateTestArray>("testarray")
          .argDescs({ ArgDescBuilder("start_idx").type(ArgType::INT).label("Start Index").valueRange(0,1000) })
          .numRequiredInput(0)
          .numMaxInput(0);
      }
    };
    OpRegistry::instance().add(CreateTestArray::mkDesc());

    std::unique_ptr<OpGraph>   proot(newGraph("root"));
    proot->newContext();
    std::ifstream gjson("tests/testgraph.json");
    auto json = nlohmann::json::parse(gjson);
    CHECK(proot->load(json));
    auto dc = proot->evalNode("join8");
    CHECK(dc->numRows(0)==9216);
  }
  CHECK(Stats::livingCount() == 0);
}

#ifndef DFCORE_STATIC
TEST_CASE("OpGraph.DynamicOp")
{
  using namespace joyflow;
  String dllpath = defaultOpDir() +
#ifdef _WIN32
    "\\oplib.dll";
#else
    "/liboplib.so";
#endif
  spdlog::info("loading {}", dllpath);
  CHECK(openOpLib(dllpath));
  auto csvreader = OpRegistry::instance().createOp("csv_reader");
  CHECK(*csvreader);
  OpRegistry::instance().destroyOp(csvreader);
}
#endif
