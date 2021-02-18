#include "opbuiltin.h"
#include "def.h"
#include "error.h"
#include "traits.h"
#include "oparg.h"
#include "opdesc.h"
#include "opkernel.h"
#include "opcontext.h"
#include "datatable.h"
#include "ophelper.h"
#include "luabinding.h"
#include "runtime.h"
#include "profiler.h"

#include <pdqsort.h>
#include <fast_float/fast_float.h>
#include <glm/glm.hpp>
#include <sol/sol.hpp>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <numeric>
#include <regex>

BEGIN_JOYFLOW_NAMESPACE

namespace op {

// Noop {{{
class Noop : public joyflow::OpKernel
{
public:
  static OpDesc desc()
  {
    return joyflow::makeOpDesc<Noop>("noop").numRequiredInput(0).numMaxInput(1).icon(/*ICON_FA_DOLLY "\xEF\x91\xB2"*/ "");
  }
  void eval(joyflow::OpContext& ctx) const override
  {
    PROFILER_SCOPE("noop", 0xbdaead);
    if (ctx.getNumInputs() > 0 && ctx.fetchInputData(0) != nullptr) {
      ctx.copyInputToOutput(0, 0);
    } else {
      ctx.reallocOutput(0);
    }
  }
};
// }}}

class Missing : public joyflow::OpKernel
{
public:
  static OpDesc desc()
  {
    return joyflow::makeOpDesc<Noop>("missing").numRequiredInput(0).numMaxInput(4).icon(/*ICON_FA_FROWN*/ "\xEF\x84\x99");
  }
};

// Join {{{
/// Combines data collections together
/// collections are merged into the first input
class Join : public OpKernel
{
public:
  static OpDesc desc()
  {
    return makeOpDesc<Join>("join")
            .icon(/*ICON_FA_HANDSHAKE*/ "\xEF\x8A\xB5")
            .numMaxInput(4)
            .numRequiredInput(1)
            .get();
  }

  virtual void eval(OpContext& context) const override
  {
    PROFILER_SCOPE("join", 0x1ba784);
    for (sint i=0, n=context.getNumInputs(); i<n; ++i) {
      if (context.hasInput(i))
        context.requireInput(i);
    }
    auto* odc = context.copyInputToOutput(0);
    for (sint i=0, n=odc->numTables(); i<n; ++i) {
      auto* table = odc->getTable(i);
      table->makeUnique();
      for (auto const& name: table->columnNames()) {
        table->getColumn(name)->makeUnique();
      }
    }
    for (int i=1; i<context.getNumInputs(); ++i) {
      if (!context.hasInput(i))
        continue;
      auto* dc = context.fetchInputData(i);
      if (dc)
        odc->join(dc);
    }
  }
};
// }}}

// Sort {{{
/// Sort a table by sepecified key (column)
class Sort : public OpKernel
{
  static constexpr char const* const NONE_COLUMN = "---NONE---";

public:
  static OpDesc desc()
  {
    return makeOpDesc<Sort>("sort")
            .icon(/*ICON_FA_SORT_ALPHA_UP*/"\xEF\x85\x9E")
            .numMaxInput(1)
            .inputPinNames({"data to sort"})
            .outputPinNames({"sorted data"})
            .argDescs({
                tableSelectionArg("table", "Table", false),
                columnSelectionArg("table", "key", "Sort Key"),
                columnSelectionArg("table", "secondkey", "Secondary Sort Key", "", {NONE_COLUMN}),
                ArgDescBuilder("order").type(ArgType::MENU).menu({"Ascending", "Descending"}),
                ArgDescBuilder("stable").type(ArgType::TOGGLE).description("Need Stable Sort?")
            })
            .get();
  }

  virtual void eval(OpContext& context) const override
  {
    PROFILER_SCOPE("sort", 0x41ae3c);
    sint   argTable  = context.arg("table").asInt();         // which table?
    String argKey    = context.arg("key").asString();        // sort key
    String argKey2   = context.arg("secondkey").asString();  // secondary sort key
    sint   argOrder  = context.arg("order").asInt();         // 0: ascending; 1: descending
    bool   argStable = context.arg("stable").asBool();       // stable sort?

    auto*  odc   = context.copyInputToOutput(0);
    if (context.fetchInputData(0)->numTables() == 0)
      return;

    auto* table = odc->getTable(argTable);
    ALWAYS_ASSERT(argTable>=0 && argTable<odc->numTables());
    ALWAYS_ASSERT(table);
    table->makeUnique();

    // temp value for numeric tuple compare
    double va[MAX_TUPLE_SIZE] = {0};
    double vb[MAX_TUPLE_SIZE] = {0};

    auto compareFunc = [&va, &vb, table, argTable](String const& key) {
      auto* column = table->getColumn(key);
      RUNTIME_CHECK(column, "column {} was not found in table {}", key, argTable);

      std::function<int(sint, sint)> compare;

      if (auto* ni = column->asNumericData()) {
        PROFILER_SCOPE("std::sort", 0xFF8C31);
        if (column->tupleSize()==1) {
          spdlog::debug("sort with fast numeric access");
#define DATATYPE_CASE(TYPE)                                                             \
          case TypeInfo<TYPE>::dataType: {                                              \
            auto const* arr = ni->rawBufferRO<TYPE>(CellIndex(0), table->numIndices()); \
            compare = [column, table, arr](sint a, sint b) -> int {                     \
              auto x = arr[table->getIndex(a).value()], y = arr[table->getIndex(b).value()]; \
              return x < y ? -1 : x > y ? 1 : 0;                                        \
            };                                                                          \
            break;                                                                      \
          }

          switch(column->dataType()) {
          DATATYPE_CASE(int32_t)
          DATATYPE_CASE(int64_t)
          DATATYPE_CASE(uint32_t)
          DATATYPE_CASE(uint64_t)
          DATATYPE_CASE(float)
          DATATYPE_CASE(double)
          default:
            spdlog::error("don't know how to compare numeric data of type {}", column->dataType());
            RUNTIME_CHECK(false, "unknown numeric data");
            break;
          }
#undef DATATYPE_CASE
        } else {
          // TODO: optimize this
          spdlog::debug("sort with numeric tuple");
          sint   tupleSize = column->tupleSize();
          auto const* numinterface = column->asNumericData();
          compare = [numinterface, table, tupleSize, &va, &vb](sint a, sint b) -> int
          {
            size_t outlen = 0;
            numinterface->getDoubleArray(va, outlen, table->getIndex(a).value()*tupleSize, tupleSize);
            numinterface->getDoubleArray(vb, outlen, table->getIndex(b).value()*tupleSize, tupleSize);
            for (int i=0; i<tupleSize; ++i) {
              if (va[i]<vb[i])
                return -1;
              else if (va[i]>vb[i])
                return 1;
            }
            return 0;
          };
        }
      } else if (column->asStringData()) {
        spdlog::debug("sort strings");
        compare = [column, table](sint a, sint b) -> int
        {
          return cmp(column->get<StringView>(table->getIndex(a)), column->get<StringView>(table->getIndex(b)));
        };
      } else {
        RUNTIME_CHECK(false, "Don\'t know how to sort {}", key);
      }

      return compare;
    };

    std::function<int(sint,sint)> primaryCompare = compareFunc(argKey);
    std::function<bool(sint, sint)> lessThan;
    if (argKey2 != argKey && !argKey2.empty() && argKey2!=NONE_COLUMN) {
      spdlog::debug("sorting with primary key \"{}\" and secondary key \"{}\"", argKey, argKey2);
      std::function<int(sint,sint)> secondaryCompare = compareFunc(argKey2);
      lessThan = [primaryCompare, secondaryCompare](sint a, sint b) -> bool {
        int pc = primaryCompare(a,b);
        if (pc==0) {
          return secondaryCompare(a,b)<0;
        } else {
          return pc<0;
        }
      };
    } else {
      spdlog::debug("sorting with primary key \"{}\"", argKey);
      lessThan = [primaryCompare](sint a, sint b)->bool {return primaryCompare(a, b) < 0; };
    }

    sint const nrows = odc->numRows(argTable);
    Vector<sint> order(nrows);
    std::iota(order.begin(), order.end(), 0);

    if (argStable) {
      std::stable_sort(order.begin(), order.end(), lessThan);
    } else {
      pdqsort(order.begin(), order.end(), lessThan);
    }

    if (argOrder == 1) // descending, inverse
      for (sint i=0; i<nrows/2; ++i)
        std::swap(order[i], order[nrows-i-1]);

    table->sort(order);
  }
};
// }}}

// Split {{{
class Split : public OpKernel
{
public:
  static OpDesc desc()
  {
    return makeOpDesc<Split>("split")
      .icon(/*ICON_FA_FILTER*/"\xEF\x82\xB0")
      .numMaxInput(1)
      .numOutputs(2)
      .argDescs({
          tableSelectionArg("table", "Table", false),
          ArgDescBuilder("condition").label("Condition").type(ArgType::STRING),
          ArgDescBuilder("inverse").label("Inverse").type(ArgType::TOGGLE)
       })
      .get();
  }

  void eval(OpContext& context) const override
  {
    PROFILER_SCOPE("split", 0x7a7374);
    auto* indata = context.fetchInputData(0);

    sint   argTable = context.arg("table").asInt();   // which table?
    String conditionExpr = context.arg("condition").asString();
    bool   inverse = context.arg("inverse").asBool();

    // on error: everything goes into pin 0
    if (conditionExpr.empty()||
        argTable < 0 ||
        argTable >= indata->numTables()) {
      if (!inverse) {
        context.copyInputToOutput(0, 0);
        context.reallocOutput(1);
      } else {
        context.copyInputToOutput(1, 0);
        context.reallocOutput(0);
      }
      return;
    }

    auto* dc0 = context.outputIsActive(0) ? context.copyInputToOutput(0, 0) : nullptr;
    auto* dc1 = context.outputIsActive(1) ? context.copyInputToOutput(1, 0) : nullptr;

    auto* intable = indata->getTable(argTable);
    auto* tb0 = dc0 ? dc0->getTable(argTable) : nullptr;
    auto* tb1 = dc1 ? dc1->getTable(argTable) : nullptr;

    // no output is active
    if (tb0 == nullptr && tb1 == nullptr)
      return;

    if (tb0)
      tb0->makeUnique();
    if (tb1)
      tb1->makeUnique();

    auto tbPass    = inverse ? tb1 : tb0;
    auto tbNotPass = inverse ? tb0 : tb1;
    filter(conditionExpr, intable, [tbPass, tbNotPass](sint row, CellIndex idx, bool pass){
      if (pass && tbNotPass) {
        tbNotPass->markRemoval(row);
      }
      if (!pass && tbPass) {
        tbPass->markRemoval(row);
      }
    });

    if (tb0) tb0->applyRemoval();
    if (tb1) tb1->applyRemoval();
  }
};
// }}}

// Defragment {{{
class Defragment : public OpKernel
{
public:
  static OpDesc desc()
  {
    return makeOpDesc<Defragment>("defragment")
      .numMaxInput(1)
      .argDescs({
          tableSelectionArg("table", "Table", true)
      });
  }

  void eval(OpContext& context) const override
  {
    PROFILER_SCOPE("defragment", 0xee3f4d);
    auto argTable = context.arg("table").asString();
    auto* odc = context.copyInputToOutput(0);
    if (odc->numTables() == 0)
      return;
    if (argTable=="ALL") {
      for (sint i=0, n=odc->numTables(); i<n; ++i) {
        auto* table = odc->getTable(i);
        table->makeUnique();
        for (auto const& colname: table->columnNames()) {
          table->getColumn(colname)->makeUnique();
        }
        table->defragment();
      }
    } else {
      char* pEnd = nullptr;
      sint tbid = std::strtol(argTable.c_str(), &pEnd, 10);
      RUNTIME_CHECK(tbid>=0 && tbid<odc->numTables(), "Table index({}) of of range [0, {})", tbid, odc->numTables());
      auto* table = odc->getTable(tbid);
      table->makeUnique();
      for (auto const& colname: table->columnNames()) {
        table->getColumn(colname)->makeUnique();
      }
      table->defragment();
    }
  }
};
// }}}

// Lua script {{{
static void* tracked_lua_alloc(void* ud, void* ptr, size_t osize, size_t nsize);
class LuaOpState : public OpStateBlock, public ObjectTracker<LuaOpState>
{
  lua_State* L = nullptr;
  OpContext* context_ = nullptr; // should always destory after me
  size_t memUsage_ = 0;
  friend void* tracked_lua_alloc(void* ud, void* ptr, size_t osize, size_t nsize);
public:
  LuaOpState(OpContext* ctx) : context_(ctx)
  {
    PROFILER_SCOPE("lua init", 0xD6ECF0);
    L = lua_newstate(tracked_lua_alloc, this);
    sol::set_default_state(L);
    joyflow::bindLuaTypes(L);
  }
  ~LuaOpState()
  {
    lua_close(L);
    // memUsage_ should be exactly 0 here
  }

  String name() const { return fmt::format("lua for [{}]", context_->node()->name()); }
  size_t size() const { return memUsage_ + sizeof(*this); }
  lua_State* interpreter() const { return L; }
};

static struct LuaOpInspectorRegister
{
  LuaOpInspectorRegister()
  {
    Stats::setInspector<LuaOpState>({
      /*name*/ [](void const* p) { return static_cast<LuaOpState const*>(p)->name(); },
      /*size*/ [](void const* p) { return static_cast<LuaOpState const*>(p)->size(); }
    });
  }
} _luaopreg;

static void* tracked_lua_alloc(void* ud, void* ptr, size_t osize, size_t nsize)
{
  auto* state = static_cast<LuaOpState*>(ud);
  state->memUsage_ += nsize;
  if (ptr)
    state->memUsage_ -= osize;
  if (nsize == 0) {
    mi_free(ptr);
    return nullptr;
  }
  return mi_realloc(ptr, nsize);
}

class LuaScript : public OpKernel
{
public:
  static OpDesc desc()
  {
    return makeOpDesc<LuaScript>("lua")
      .numMaxInput(4)
      .numRequiredInput(0)
      .numOutputs(1)
      .argDescs({
          ArgDescBuilder("code").label("Code").codeLanguage("lua").type(ArgType::CODEBLOCK)
       })
      .icon("\xEF\x84\xA1"/*FA_ICON_CODE*/);
  }

  void eval(OpContext &ctx) const override
  {
    PROFILER_SCOPE("lua", 0xf8f4ed);
    auto script = ctx.arg("code").asString();
    lua_State* L = nullptr;
    /*
    {
      PROFILER_SCOPE("lua init", 0xD6ECF0);
      L = luaL_newstate();
      joyflow::bindLuaTypes(L);
    }
    DEFER([L] {lua_close(L); });
    */
    {
      auto *state = static_cast<LuaOpState*>(ctx.getState());
      if (!state) {
        state = new LuaOpState(&ctx);
        ctx.setState(state);
      }
      L = state->interpreter();
    }
    sol::state_view lua(L);
    lua["ctx"]  = &ctx;
    // spdlog::info("evaluation lua script of node {}", ctx.nodeName());

    DataCollection* in = ctx.hasInput(0) ? ctx.fetchInputData(0) : nullptr;
    DataCollection* out = nullptr;
    if (in) {
      out = ctx.copyInputToOutput(0,0);
      for (sint i=0, n=out->numTables(); i<n; ++i)
        out->getTable(i)->makeUnique();
    } else {
      out = ctx.reallocOutput(0);
    }
    lua["data"] = out;

    try {
      lua.safe_script(script, sol::script_throw_on_error);
    } catch (sol::error const& e) {
      ctx.reportError(e.what(), OpErrorLevel::ERROR, true);
    } catch(CheckFailure const& e) {
      ctx.reportError(e.what(), OpErrorLevel::ERROR, true);
    } catch(AssertionFailure const& e) {
      ctx.reportError(e.what(), OpErrorLevel::ERROR, true);
    }
    lua.collect_gc();
  }
};
// }}}

// String Cast {{{
template <class T>
static void strColumnConv(DataTable* odt, DataColumn* origColumn, DataColumn* tempColumn)
{
  for (size_t i=0, n=odt->numIndices(); i<n; ++i) {
    CellIndex ci{i};
    if (odt->getRow(ci)==-1)
      continue;
    else {
      auto sv = origColumn->get<StringView>(ci);
      if (sv.data() && sv.size()) {
        T val=0;
        if constexpr (std::is_integral<T>::value) {
          if (auto [p, ec] = std::from_chars(sv.data(), sv.data()+sv.size(), val); ec==std::errc())
            tempColumn->template set<T>(ci, val);
        } else {
          // libstdc++ still cannot convert floating point strings
          if (auto [p, ec] = fast_float::from_chars(sv.data(), sv.data()+sv.size(), val); ec==std::errc())
            tempColumn->template set<T>(ci, val);
        }
      }
    }
  }
}

class StringCast : public OpKernel
{
public:
  static OpDesc desc()
  {
    return makeOpDesc<StringCast>("string_cast")
      .numMaxInput(1).numRequiredInput(1).numOutputs(1)
      .icon(/*ICON_FA_EXCHANGE_ALT*/"\xEF\x8D\xA2")
      .argDescs({
        ArgDescBuilder("dst_type").label("To Type").type(ArgType::MENU).menu({"int32", "int64", "float", "double"}).defaultExpression(0,"int32"),
        tableSelectionArg("table", "Table", false),
        columnSelectionArg("table", "columns", "Columns").type(ArgType::MULTI_MENU).tupleSize(0),
      });
  }

  void eval(OpContext& ctx) const override
  {
    sint   const tableidx    = ctx.arg("table").asInt();
    auto   const columnNames = ctx.arg("columns").asStringList();
    String const destType    = ctx.arg("dst_type").asString();
    auto*        odc         = ctx.copyInputToOutput(0, 0);

    if (columnNames.empty()) // nothing to do
      return;

    RUNTIME_CHECK(odc, "no output data");
    RUNTIME_CHECK(tableidx>=0 && tableidx<odc->numTables(), "table {} out of bound [0, {})", tableidx, odc->numTables());

    auto*  odt = odc->getTable(tableidx);
    RUNTIME_CHECK(odt, "table {} was not found", tableidx);
    odt->makeUnique();

    Vector<marl::Event> eventsToWait;
    Vector<String>      tempnames;
    bool const fireTask = columnNames.size() > 1 && odt->numIndices()>100; // TODO: tune these conditions
    for (auto const& columnName : columnNames) {
      // TODO: exceptions should be catched within task
      marl::Event evt;
      String tempname = tempnames.emplace_back("##converting_" + columnName);
      DataColumnPtr origColumn = odt->getColumn(columnName);
      DataColumnPtr tempcolumn = nullptr;
      if (destType == "int32") {
        tempcolumn = odt->createColumn<int32_t>(tempname, 0);
      } else if (destType == "int64") {
        tempcolumn = odt->createColumn<int64_t>(tempname, 0);
      } else if (destType == "float") {
        tempcolumn = odt->createColumn<float>(tempname, 0.f);
      } else if (destType == "double") {
        tempcolumn = odt->createColumn<double>(tempname, 0.0);
      } else {
        throw TypeError(fmt::format("unknown type {}", destType));
      }
      RUNTIME_CHECK(origColumn, "column \"{}\" of table {} was not found", columnName, tableidx);
      RUNTIME_CHECK(origColumn->asStringData(), "column \"{}\" of table {} has no string interface", columnName, tableidx);

      auto task = [odt, evt, origColumn, tempcolumn, &destType]() {
        PROFILER_SCOPE("StringConversion", 0xD9B611);
        try {
          if (destType == "int32") {
            strColumnConv<int32_t>(odt, origColumn.get(), tempcolumn.get());
          } else if (destType == "int64") {
            strColumnConv<int64_t>(odt, origColumn.get(), tempcolumn.get());
          } else if (destType == "float") {
            strColumnConv<float>(odt, origColumn.get(), tempcolumn.get());
          } else if (destType == "double") {
            strColumnConv<double>(odt, origColumn.get(), tempcolumn.get());
          }
        } catch (std::exception const& e) {
          spdlog::error("string_cast: exception caught during task evaluation: {}", e.what());
        }
        evt.signal();
      };
      if (fireTask) {
        eventsToWait.push_back(evt);
        TaskContext::instance().scheduler.enqueue(marl::Task(task));
      } else {
        task();
        odt->renameColumn(tempname, columnName, true);
      }
    }
    if (fireTask && !eventsToWait.empty()) {
      RUNTIME_CHECK(eventsToWait.size() == tempnames.size(), "task count mismatched");
      RUNTIME_CHECK(eventsToWait.size() == columnNames.size(), "column count mismatched");
      for (size_t i = 0; i < eventsToWait.size(); ++i) {
        eventsToWait[i].wait();
      }
      for (size_t i = 0; i < tempnames.size(); ++i) {
        odt->renameColumn(tempnames[i], columnNames[i]);
      }
    }
  }
};
// String Cast }}}

// Collect {{{
// put tables from other data sources into self
class Collect : public OpKernel
{
public:
  static OpDesc desc()
  {
    return makeOpDesc<Collect>("collect")
      .icon(/*ICON_FA_DATABASE*/"\xEF\x87\x80")
      .numMaxInput(4).numRequiredInput(1).numOutputs(1);
  }
  void eval(OpContext& ctx) const override
  {
    for (sint i=0; i<ctx.getNumInputs(); ++i) {
      ctx.requireInput(i);
    }
    auto* odc = ctx.copyInputToOutput(0,0);
    for (sint i=1; i<ctx.getNumInputs(); ++i) {
      if (ctx.hasInput(i)) {
        if (auto* idc = ctx.fetchInputData(i)) {
          for (sint t=0; idc && t<idc->numTables(); ++t) {
            odc->addTable(idc->getTable(t));
          }
        }
      }
    }
  }
};
// Collect }}}

// If {{{
class IfStmt : public OpKernel
{
public:
  static OpDesc desc()
  {
    return makeOpDesc<IfStmt>("if").numMaxInput(2).numRequiredInput(0).numOutputs(1)
      .icon(/*ICON_FA_CODE_BRANCH*/ "\xEF\x84\xA6")
      .argDescs({
        ArgDescBuilder("condition").type(ArgType::STRING).label("Condition").defaultExpression(0, "true")
          .description("lua expression\n\n  if evaluated to be true, first input will be passed to output\n  otherwise second input will be passed to output")
    });
  }

  void eval(OpContext& ctx) const override
  {
    // sol::state lua;
    // bindLuaTypes(lua, true);
    auto *state = static_cast<LuaOpState*>(ctx.getState());
    if (!state) {
      state = new LuaOpState(&ctx);
      ctx.setState(state);
    }
    sol::state_view lua(state->interpreter());

    String expr = ctx.arg("condition").asString();
    if (std::regex_match(expr, std::regex("\\W?data[^a-zA-Z0-9_]+"))) {
      lua["data"] = ctx.fetchInputData(0);
    }
    if (lua.safe_script(fmt::format("return not not ({})", expr), sol::script_throw_on_error)) {
      if (ctx.hasInput(0))
        ctx.copyInputToOutput(0,0);
      else
        ctx.reallocOutput(0);
    } else {
      if (ctx.hasInput(1))
        ctx.copyInputToOutput(0,1);
      else
        ctx.reallocOutput(0);
    }
  }
};
// If }}}

// Column Rename  {{{
class ColumnRename : public OpKernel
{
public:
  static OpDesc desc()
  {
    return makeOpDesc<ColumnRename>("rename_column").numMaxInput(1).numOutputs(1)
      .icon(/*ICON_FA_I_CURSOR*/ "\xEF\x89\x86")
      .argDescs({
        tableSelectionArg("table", "Table", false),
        columnSelectionArg("table", "column", "Column"),
        ArgDescBuilder("newname").label("New Name").type(ArgType::STRING),
        ArgDescBuilder("overwrite").label("Overwrite").type(ArgType::TOGGLE).description("Write to new name even if there exists one").defaultExpression(0, "false")
      });
  }
  void eval(OpContext& context) const override
  {
    sint   const tid = context.arg("table").asInt();
    String const name = context.arg("column").asString();
    String const newname = context.arg("newname").asString();
    bool   const overwrite = context.arg("overwrite").asBool();
    auto *idc = context.fetchInputData(0);
    RUNTIME_CHECK(idc, "no input data");
    auto *odc = context.copyInputToOutput(0, 0);
    if (name.empty()) {
      context.reportError("no source column specified", OpErrorLevel::WARNING, false);
      return;
    }
    if (newname.empty()) {
      context.reportError("no destiny column name specified", OpErrorLevel::WARNING, false);
      return;
    }
    auto *odt = odc->getTable(tid);
    RUNTIME_CHECK(odt, "no table numbered {} exists", tid);
    RUNTIME_CHECK(odt->getColumn(name), "no column named {} exists in table {}", name, tid);
    odt->makeUnique();
    if (!odt->renameColumn(name, newname, overwrite))
      context.reportError("failed for some reason", OpErrorLevel::WARNING, false);
  }
};
// Column Rename }}}

// Add Table {{{
class AddTable : public OpKernel
{
public:
  static OpDesc desc()
  {
    return makeOpDesc<AddTable>("add_table").numMaxInput(1).numRequiredInput(0).numOutputs(1)
      .icon(/*ICON_FA_PLUS_SQUARE*/ "\xEF\x83\xBE")
      .argDescs({
        ArgDescBuilder("count").label("Count").description("number of tables to add").defaultExpression(0, "1").type(ArgType::INT).closeRange(true,true).valueRange(0, 10)
      });
  }

  void eval(OpContext& context) const override
  {
    auto cnt = context.arg("count").asInt();
    DEBUG_ASSERT(cnt >= 0 && cnt <= 10000000);
    auto* odc = context.copyInputToOutput(0, 0);
    for (sint i = 0; i < cnt; ++i) {
      odc->addTable();
    }
  }
};
// Add Table }}}

// Add Column {{{
class AddColumn : public OpKernel
{
public:
  static OpDesc desc()
  {
    return makeOpDesc<AddColumn>("add_column").numMaxInput(1).numRequiredInput(1).numOutputs(1)
      .icon(/*ICON_FA_PLUS_CIRCLE*/ "\xEF\x81\x95")
      .argDescs({
        tableSelectionArg("table", "Table", false),
        ArgDescBuilder("name").label("Name").defaultExpression(0, "new_column").type(ArgType::STRING),
        ArgDescBuilder("type").label("Type").type(ArgType::MENU).description("storage type").menu({
          "int32_t", "uint32_t", "int64_t", "uint64_t", "float", "double", "string"
        }).defaultExpression(0,"float"),
        ArgDescBuilder("tupleSize").label("Tuple").type(ArgType::INT).description("tuple size").valueRange(1,4).defaultExpression(0, "1"),
        ArgDescBuilder("array").label("Array").type(ArgType::TOGGLE).defaultExpression(0, "false"),
        ArgDescBuilder("overwrite").label("Overwrite").defaultExpression(0, "true").type(ArgType::TOGGLE)
      });
  }

  void eval(OpContext& context) const override
  {
    auto tid = context.arg("table").asInt();
    auto name = context.arg("name").asString();
    auto type = context.arg("type").asString();
    auto tuplesize = context.arg("tupleSize").asInt();
    auto isarray   = context.arg("array").asBool();
    auto overwrite = context.arg("overwrite").asBool();

    auto* odc = context.copyInputToOutput(0, 0);
    RUNTIME_CHECK(odc, "failed to alloc output data");
    auto* odt = odc->getTable(tid);
    RUNTIME_CHECK(odt, "table {} not found", tid);
    
    DataColumnDesc cold = {};
    using IntType = std::underlying_type<DataType>::type;
    for (IntType tp = IntType(DataType::UNKNOWN); tp < IntType(DataType::COUNT); ++tp) {
      if (dataTypeName(DataType(tp))==type) {
        cold.dataType = DataType(tp);
        break;
      }
    }
    cold.tupleSize = tuplesize;
    if (cold.dataType == DataType::STRING || cold.dataType == DataType::BLOB)
      cold.fixSized = false;
    else
      cold.fixSized = true;
    cold.container = isarray;

    RUNTIME_CHECK(cold.isValid(), "invalid column format");
    odt->createColumn(name, cold, overwrite);
  }
};
// Add Column }}}

// Add Rows {{{
class AddRows : public OpKernel
{
public:
  static OpDesc desc()
  {
    return makeOpDesc<AddRows>("add_rows")
      .icon(/*ICON_FA_PLUS*/ "\xEF\x81\xA7")
      .numMaxInput(1)
      .argDescs({
        tableSelectionArg("table", "Table", false),
        ArgDescBuilder("count").label("Count").type(ArgType::INT).defaultExpression(0,"1").valueRange(0,100).closeRange(false,false)
      });
  }

  void eval(OpContext& ctx) const override
  {
    auto tid = ctx.arg("table").asInt();
    auto cnt = ctx.arg("count").asInt();

    auto* odc = ctx.copyInputToOutput(0, 0);
    RUNTIME_CHECK(odc, "failed to alloc output data");
    auto* odt = odc->getTable(tid);
    RUNTIME_CHECK(odt, "table {} not found", tid);

    odt->addRows(cnt);
  }
};
// }}}

// Column Duplication {{{
class ColumnDuplicate : public OpKernel
{
public:
  static OpDesc desc()
  {
    return makeOpDesc<ColumnDuplicate>("duplicate_column").numMaxInput(1).numOutputs(1)
      .icon(/*ICON_FA_CLONE*/ "\xEF\x89\x8D")
      .argDescs({
        tableSelectionArg("table", "Table", false),
        columnSelectionArg("table", "column", "Column"),
        ArgDescBuilder("newname").label("Name").description("New name for the duplicated column").type(ArgType::STRING).defaultExpression(0, "dup"),
        ArgDescBuilder("overwrite").label("Overwrite").description("Overwrite existing column if exists").type(ArgType::TOGGLE).defaultExpression(0, "true")
      });
  }
  void eval(OpContext& context) const override
  {
    sint const tid     = context.arg("table").asInt();
    auto const colname = context.arg("column").asString();
    auto const newname = context.arg("newname").asString();
    auto const overwrite = context.arg("overwrite").asBool();

    if (!context.hasInput(0)) {
      context.setOutputData(0, nullptr);
      return;
    }

    auto* odc = context.copyInputToOutput(0, 0);
    auto* odt = odc->getTable(tid);
    RUNTIME_CHECK(odt, "table {} cannot be found", tid);
    auto* srccol = odt->getColumn(colname);
    RUNTIME_CHECK(srccol, "cannot find column {} in table {}", colname, tid);
    RUNTIME_CHECK(!newname.empty(), "name not specified");

    auto dupcol = srccol->share();
    odt->setColumn(newname, dupcol.get());
  }
};
// Column Duplication }}}

// Column Removal {{{
class ColumnRemove : public OpKernel
{
public:
  static OpDesc desc()
  {
    return makeOpDesc<ColumnRemove>("remove_column").numMaxInput(1).numOutputs(1)
      .icon(/*ICON_FA_MINUS_CIRCLE*/ "\xEF\x81\x96")
      .argDescs({
        tableSelectionArg("table", "Table", false),
        columnSelectionArg("table", "columns", "Columns").type(ArgType::MULTI_MENU),
      });
  }
  void eval(OpContext& context) const override
  {
    sint  const tid     = context.arg("table").asInt();
    auto const& columns = context.arg("columns").asStringList();
    auto *odc = context.copyInputToOutput(0, 0);
    if (columns.empty()) {
      return;
    }
    auto *odt = odc->getTable(tid);
    RUNTIME_CHECK(odt, "no table numbered {} exists", tid);
    odt->makeUnique();
    for (auto const& name : columns) {
      RUNTIME_CHECK(odt->getColumn(name), "no column named {} exists in table {}", name, tid);
      if (!odt->removeColumn(name))
        context.reportError("failed for some reason", OpErrorLevel::WARNING, false);
    }
  }
};
// Column Reomval }}}

// Table Removal (Drop) {{{
class DropTable : public OpKernel
{
public:
  static OpDesc desc()
  {
    return makeOpDesc<DropTable>("drop_table")
      .icon(/*ICON_FA_MINUS_SQUARE*/ "\xEF\x85\x86")
      .numRequiredInput(1)
      .numMaxInput(1)
      .numOutputs(1)
      .argDescs({
        tableSelectionArg("tables", "Tables to Drop", false).type(ArgType::MULTI_MENU).tupleSize(0)
      });
  }

  void eval(OpContext &context) const override
  {
    auto *odc = context.copyInputToOutput(0, 0);
    auto const& tables = context.arg("tables").asStringList();
    if (tables.empty())
      return;
    Vector<sint> tableIdsToRemove;
    for (auto const& tb: tables) {
      sint tid = std::stol(tb);
      tableIdsToRemove.push_back(tid);
    }
    pdqsort(tableIdsToRemove.begin(), tableIdsToRemove.end(), std::greater<sint>());
    // delete in reverse order
    for (sint tid: tableIdsToRemove) {
      odc->removeTable(tid);
    }
  }
};
// Table Removal (Drop) }}}

// Loop {{{
class LoopOpState : public OpStateBlock
{
public:
  std::set<Pair<OpContext*,sint>> affected;
  DataCollectionPtr feedback      = nullptr;
  sint              loopIteration = 0;
  sint              loopCount     = 0;
  bool              inputDirty    = false;
};

class LoopController : public OpKernel
{
public:
  static OpDesc desc()
  {
    return makeOpDesc<LoopController>("loop")
      .numMaxInput(1).numRequiredInput(0).numOutputs(1)
      .icon(/*ICON_FA_RETWEET*/ "\xEF\x81\xB9")
      .argDescs({
        ArgDescBuilder("count").label("Count").type(ArgType::INT).defaultExpression(0, "10").valueRange(0, 100).closeRange(true, false)
      });
  }
  static void reset(LoopOpState* state)
  {
    state->feedback = nullptr;
    state->loopIteration = 0;
    state->loopCount = 0;
    state->affected.clear();
  }
  void bind(OpContext& context) override
  {
    LoopOpState* state = static_cast<LoopOpState*>(context.getState());
    if (!state) {
      state = new LoopOpState;
      context.setState(state);
    }
  }
  void beforeFrameEval(OpNode* node) override
  {
    DEBUG_ASSERT(node && node->context() && node->context()->getState());
  }
  void afterFrameEval(OpNode* node) override
  {
    DEBUG_ASSERT(node && node->context());
    if (auto* state = static_cast<LoopOpState*>(node->context()->getState())) {
      reset(state);
    }
  }
  void eval(OpContext& ctx) const override
  {
    if (!ctx.hasInput(0)) {
      ctx.reallocOutput(0);
      return;
    }
    LoopOpState* state = static_cast<LoopOpState*>(ctx.getState());
    RUNTIME_CHECK(state, "loop state should be initialized before eval");
    state->loopCount = ctx.arg("count").asInt();
    for (state->loopIteration=0; state->loopIteration<state->loopCount; ++state->loopIteration) {
      for (auto const [c,p]: state->affected) {
        c->markDirty(true);
      }
      spdlog::debug("loop: iteration {}", state->loopIteration);
      state->feedback = ctx.fetchInputData(0);
    }
    ctx.setOutputData(0, state->feedback);
    state->feedback = nullptr;
  }
  void afterEval(OpContext& ctx) const override
  {
    LoopOpState* state = static_cast<LoopOpState*>(ctx.getState());
    RUNTIME_CHECK(state, "loop state should be initialized before eval");
    if (state->inputDirty) {
      ctx.markDirty();
    }
  }
};

class LoopControllee
{
protected:
  OpNode*         ctrlNode_ = nullptr;
  OpContext*      ctrlCtx_ = nullptr;
  LoopController* ctrl_ = nullptr;
  LoopOpState*    ctrlState_ = nullptr;
public:
  static ArgDesc controllerArg()
  {
    return ArgDescBuilder("controller").type(ArgType::OPREF).label("Controller").description("Controller Reference");
  }
  void initController(OpNode* node, sint controllerPin=-1)
  {
    ctrlCtx_ = nullptr;
    ctrl_ = nullptr;
    ctrlState_ = nullptr;
    ctrlNode_ = nullptr;

    ALWAYS_ASSERT(node != nullptr);
    if (controllerPin >= 0 && controllerPin < node->upstreams().ssize()) {
      ctrlNode_ = node->parent()->node(node->upstreams()[controllerPin].name);
    } else {
      auto ctrlname = node->arg("controller").asString();
      ctrlNode_ = node->parent()->node(ctrlname);
    }
    if (!ctrlNode_)
      return;
    ctrlCtx_ = ctrlNode_->context();
    if (!ctrlCtx_)
      return;
    ctrl_ = dynamic_cast<LoopController*>(*ctrlCtx_->getKernel());
    if (!ctrl_)
      return;
    ctrlState_ = static_cast<LoopOpState*>(ctrlCtx_->getState());
    if (!ctrlState_)
      return;

    Vector<Pair<OpNode*,sint>> searchStack;
    HashSet<OpNode*> visitedNodes;
    for(searchStack.emplace_back(node, 0); !searchStack.empty(); ) {
      auto [top,pin] = searchStack.pop_back();
      auto *topctx = top->context();
      if (!topctx) // not at upstream of the loop controller
        continue;
      if (visitedNodes.find(top) != visitedNodes.end())
        continue;
      ctrlState_->affected.insert({ topctx, pin });
      visitedNodes.insert(top);

      for (auto const& downstreamPin : top->downstreams()) {
        for (auto const& downstreamConnection : downstreamPin) {
          auto* afnode = node->parent()->node(downstreamConnection.name);
          RUNTIME_CHECK(afnode, "downstream {} of node {} does not exist", downstreamConnection.name, top->name());
          searchStack.emplace_back(afnode, downstreamConnection.pin);
        }
      }
    }
  }
};

class LoopFeedback : public OpKernel, public LoopControllee
{
public:
  static OpDesc desc()
  {
    return makeOpDesc<LoopFeedback>("feedback")
      .numMaxInput(2).numRequiredInput(0).numOutputs(1)
      .icon(/*ICON_FA_RECYCLE*/ "\xEF\x86\xB8")
      .inputPinNames({"Initial Value", "Loop Body"})
      .flags(OpFlag::LIGHTWEIGHT | OpFlag::ALLOW_LOOP | OpFlag::LOOP_PIN1)
      .argDescs({
        // controllerArg()
      });
  }
  void beforeFrameEval(OpNode* node) override
  {
    LoopControllee::initController(node, 1);
  }
  void eval(OpContext& ctx) const override
  {
    // RUNTIME_CHECK(ctrlCtx_, "no controller specified");
    // RUNTIME_CHECK(ctrl_, "controller is not loop");
    // RUNTIME_CHECK(ctrlState_, "controller not initialized");
    RUNTIME_CHECK(!(ctrlNode_ && !ctrlState_), "Loop body (my second input) should always be a `loop` node");
    ctx.fetchInputData(0); // let it update .. althrough we may not need it right now
    if (ctrlState_ && ctx.inputDirty(0))
      ctrlState_->inputDirty = true;
    if (ctrlState_ && ctrlState_->feedback) {
      ctx.setOutputData(0, ctrlState_->feedback);
    } else if (ctx.hasInput(0)) {
      ctx.copyInputToOutput(0, 0);
    } else {
      ctx.reallocOutput(0);
    }
  }
  void afterFrameEval(OpNode* node) override
  {
    if (ctrlState_)
      ctrlState_->inputDirty = false;
  }
};

class LoopInfo : public OpKernel, public LoopControllee
{
public:
  static OpDesc desc()
  {
    return makeOpDesc<LoopInfo>("loop_info")
      .numMaxInput(0).numOutputs(1)
      .icon(/*ICON_FA_INFO_CIRCLE*/ "\xEF\x81\x9A")
      .flags(OpFlag::LIGHTWEIGHT)
      .argDescs({
        controllerArg()
      });
  }
  void beforeFrameEval(OpNode* node) override
  {
    LoopControllee::initController(node);
  }
  void eval(OpContext& ctx) const override
  {
    RUNTIME_CHECK(ctrlCtx_, "no controller specified");
    RUNTIME_CHECK(ctrl_, "controller is not loop");
    RUNTIME_CHECK(ctrlState_, "controller not initialized");
    DataTable* dt = nullptr;
    if (ctx.hasOutputCache(0)) {
      auto dc = ctx.getOutputCache(0);
      ctx.increaseOutputVersion(0);
      dt = dc->getTable(0);
    } else {
      auto dc = ctx.reallocOutput(0);
      auto tid = dc->addTable();
      dt = dc->getTable(tid);
    }
    RUNTIME_CHECK(dt, "Unable to write output data");
    spdlog::debug("loop info: iteration {}", ctrlState_->loopIteration);
    dt->setVariable("iteration", ctrlState_->loopIteration);
    dt->setVariable("numiterations", ctrlState_->loopCount);
  }
};
// Loop }}}

// Match (TODO) {{{
class Match : public OpKernel
{
public:
  static OpDesc desc()
  {
    return makeOpDesc<Match>("match")
      .numMaxInput(1).numOutputs(1)
      .argDescs({
          tableSelectionArg("dsttable", "Destiny Table", false),
          tableSelectionArg("srctable", "Source Table", false),
          columnSelectionArg("dsttable", "dstcolmatch", "Match").description("Attribute from destination table to match"),
          columnSelectionArg("srctable", "srccolmatch", "With").description("Attribute from source table to compare with"),
          columnSelectionArg("srctable", "colimport", "Import").description("Attributes to import when matched"),
          ArgDescBuilder("behavior").type(ArgType::MENU).label("Behavior").menu({
              "First Matching",
              "Last Matching",
              "Average",
              "Sum"
          }),
          ArgDescBuilder("overwrite").label("Overwrite Existing").type(ArgType::TOGGLE)
      });
  }

  void eval(OpContext& ctx) const override
  {
    sint   dsttableidx = ctx.arg("dsttable").asInt();
    sint   srctableidx = ctx.arg("srctable").asInt();
    String srcmatchcol = ctx.arg("srccolmatch").asString();
    String dstmatchcol = ctx.arg("dstcolmatch").asString();
    String importCol   = ctx.arg("colimport").asString();
    enum class Behaviour : int {
      FIRST = 0, LAST, AVERAGE, SUM
    } behavior = static_cast<Behaviour>(ctx.arg("behavior").asInt());
    bool  overwrite = ctx.arg("overwrite").asBool();
    auto* odc = ctx.copyInputToOutput(0,0);
    auto* dt = odc->getTable(dsttableidx);
    auto* st = odc->getTable(srctableidx);
    RUNTIME_CHECK(st, "Source table missing");
    RUNTIME_CHECK(dt, "Destination table missing");

    DataColumnPtr scol = st->getColumn(srcmatchcol); // match source
    DataColumnPtr dcol = dt->getColumn(dstmatchcol); // match destiny
    DataColumnPtr icol = st->getColumn(importCol); // source data
    DataColumnPtr ocol = dt->getColumn(importCol); // copy destiny
    RUNTIME_CHECK(scol!=nullptr, "Source table has no column named {}", srcmatchcol);
    RUNTIME_CHECK(dcol!=nullptr, "Destiny table has no column named {}", dstmatchcol);
    RUNTIME_CHECK(icol != nullptr, "Source table has no column named {}", importCol);
    RUNTIME_CHECK(scol->dataType() == dcol->dataType(),
        "Datatype mismatch ({} vs {})",
        dataTypeName(scol->dataType()), dataTypeName(dcol->dataType()));
    RUNTIME_CHECK(scol->tupleSize() == dcol->tupleSize(),
        "Tuplesize mismatch ({} vs {})", scol->tupleSize(), dcol->tupleSize());
    dt->makeUnique();

    // check icol & ocol has matching type
    // create ocol if none exists already
    if (!ocol) {
      ocol = dt->createColumn(importCol, icol->desc());
      ASSERT(ocol->copyInterface() && ocol->copyInterface()->copyable(icol.get())); // one should always able to copy from its own type
    } else if (ocol == icol) { // if we are writing to input, then make a copy first
      icol = icol->share();
      ocol->makeUnique();
      ASSERT(ocol->copyInterface()->copyable(icol.get())); // one should always able to copy from its own type
    } else if (!ocol->copyInterface() || !ocol->copyInterface()->copyable(icol.get())) {
      if (overwrite) {
        ocol = dt->createColumn(importCol, icol->desc(), true);
      } else {
        return;
      }
    } 
    ASSERT(ocol);
    ocol->makeUnique();
    auto * cpyifce = ocol->copyInterface();
    RUNTIME_CHECK(cpyifce && cpyifce->copyable(icol.get()), "{} cannot be copied", importCol);

    auto const* cmpifce = dcol->compareInterface();
    if (behavior != Behaviour::FIRST)
      throw Unimplemented(
          fmt::format("importing {} is not supported yet", ctx.arg("behavior").asString()));
    RUNTIME_CHECK(cmpifce && cmpifce->comparable(scol.get()), "Column \"{}\" from source and column \"{}\" from destiny are not comparable", srcmatchcol, dstmatchcol);
    if (scol->compareInterface() && scol->compareInterface()->searchable(dcol->dataType(), dcol->tupleSize(), dcol->desc().elemSize)) {
      for (CellIndex didx{ 0 }, n{ dt->numIndices() }; didx < n; ++didx) {
        if (dt->getRow(didx) == -1)
          continue;

        DataType dsttp = dcol->dataType();
        void const* rawptr = nullptr;
        size_t rawsize = 0;
        if (isNumeric(dsttp)) {
          rawptr = dcol->asNumericData()->getRawBufferRO(didx.value() * dcol->tupleSize(), dcol->tupleSize(), dsttp);
          rawsize = dcol->tupleSize() * dataTypeSize(dsttp);
        } else if (dsttp == DataType::BLOB || dsttp == DataType::STRING) {
          auto blob = dcol->get<SharedBlobPtr>(didx);
          if (blob) {
            rawptr = blob->data;
            rawsize = blob->size;
          }
        }

        if (!rawptr || rawsize == 0)
          continue;
        auto sidx = scol->compareInterface()->search(st, dsttp, rawptr, rawsize);
        if (sidx.valid() && !cpyifce->copy(didx, icol.get(), sidx)) {
          spdlog::warn("failed to copy row {} of table {} to row {} of table {}", st->getRow(sidx), srctableidx, dt->getRow(didx), dsttableidx);
        }
      }
    } else if (cmpifce->comparable(scol.get())) { // iterate through
      for (CellIndex didx{ 0 }, n{ dt->numIndices() }; didx < n; ++didx) {
        if (dt->getRow(didx) == -1)
          continue;
        for (CellIndex sidx(0); sidx < st->numIndices(); ++sidx) {
          if (st->getRow(sidx) == -1)
            continue;
          if (cmpifce->compare(didx, scol.get(), sidx) == 0) {
            if (behavior == Behaviour::FIRST) {
              if (!cpyifce->copy(didx, icol.get(), sidx)) {
                spdlog::warn("failed to copy row {} of table {} to row {} of table {}", st->getRow(sidx), srctableidx, dt->getRow(didx), dsttableidx);
              }
              break;
            }
            // TODO: other behaviors
          }
        }
      }
    }
  }
};
// Match }}}

// Iterate {{{
class Iterate: public OpKernel
{
public:
  static OpDesc desc()
  {
    return makeOpDesc<Iterate>("iterate")
      .icon(/*ICON_FA_SYNC*/ "\xEF\x80\xA1")
      .flags(OpFlag::LIGHTWEIGHT | OpFlag::ALLOW_LOOP | OpFlag::LOOP_PIN1)
      .numMaxInput(2).inputPinNames({ "Initial", "Next" }).numOutputs(2).outputPinNames({ "Previous frame", "This frame" })
      .argDescs({
        // ArgDescBuilder("fetchfrom").type(ArgType::OPREF).label("Fetch From").description("fetch and cache output from where"),
      });
  }

  struct CachedFrame: public OpStateBlock
  {
    DataCollectionPtr previous = nullptr;
  };

  void afterFrameEval(OpNode* node) override
  {
    auto* ctx = node->context();
    if (!ctx)
      return;
    if (auto* cache = static_cast<CachedFrame*>(ctx->getState())) {
      if (node->upstreams().size() != 2)
        return;
      auto ipin = node->upstreams()[1];
      DEBUG_ASSERT(ipin.isValid());
      if (auto* from = node->parent()->node(ipin.name)) {
        if (!from->context()) {
          cache->previous = nullptr;
          return;
        }
        auto ver = from->context()->outputVersion(ipin.pin);
        cache->previous = from->context()->getOutputCache(ipin.pin);
        ctx->markDirty();
      }
    }
  }

  void eval(OpContext& ctx) const override
  {
    CachedFrame* cache = static_cast<CachedFrame*>(ctx.getState());
    if (cache == nullptr) {
      cache = new CachedFrame;
      ctx.setState(cache);
    }

    auto prev = cache->previous;
    auto indata = ctx.fetchInputData(0);
    ctx.setOutputData(0, prev && !ctx.inputDirty(0) ? prev : indata);
    ctx.setOutputData(1, indata);
  }
};
// Iterate }}}

// Map (TODO) {{{
// Map (TODO) }}}

// Reduce (TODO) {{{
// Reduce }}}

}

void registerBuiltinOps()
{
  OpRegistry::instance().add(op::Join::desc());
  OpRegistry::instance().add(op::Sort::desc());
  OpRegistry::instance().add(op::Split::desc());
  OpRegistry::instance().add(op::Noop::desc());
  OpRegistry::instance().add(op::Missing::desc());
  OpRegistry::instance().add(op::Defragment::desc());
  OpRegistry::instance().add(op::LuaScript::desc());
  OpRegistry::instance().add(op::StringCast::desc());
  OpRegistry::instance().add(op::Collect::desc());
  OpRegistry::instance().add(op::DropTable::desc());
  OpRegistry::instance().add(op::IfStmt::desc());
  OpRegistry::instance().add(op::Match::desc());
  OpRegistry::instance().add(op::ColumnRename::desc());
  OpRegistry::instance().add(op::ColumnDuplicate::desc());
  OpRegistry::instance().add(op::ColumnRemove::desc());
  OpRegistry::instance().add(op::AddTable::desc());
  OpRegistry::instance().add(op::AddColumn::desc());
  OpRegistry::instance().add(op::AddRows::desc());

  OpRegistry::instance().add(op::LoopController::desc());
  OpRegistry::instance().add(op::LoopFeedback::desc());
  OpRegistry::instance().add(op::LoopInfo::desc());

  OpRegistry::instance().add(op::Iterate::desc());
}

END_JOYFLOW_NAMESPACE
