#pragma once
#include "def.h"
#include "error.h"
#include "datatable.h"
#include "oparg.h"
#include "profiler.h"

#include <sstream>
#include <iomanip>
#include <regex>

BEGIN_JOYFLOW_NAMESPACE

namespace op {

// Common update scripts {{{
inline ArgDescBuilder tableSelectionArg(String const& argname = "table", String const& label = "Table", bool canSelectAll = true) {
  return ArgDescBuilder(argname).label(label).type(ArgType::MENU).defaultExpression(0, "0")
  .updateScript(fmt::format(R"(
    local idata = ctx:inputData(0)
    local menuitems = {{ {0} }}
    if idata and idata:numTables()>0 then
      for i=0,idata:numTables()-1 do
        table.insert(menuitems, tostring(i))
      end
    end
    self:desc():setMenu(menuitems)
  )", canSelectAll?"'ALL'":""));
}

inline ArgDescBuilder columnSelectionArg(String const& table, String const& argname, String const& label="", String const& description="", Vector<String> const& extraMenu={}) {
  Vector<String> quotedMenu(extraMenu.size());
  std::transform(extraMenu.begin(), extraMenu.end(), quotedMenu.begin(), [](std::string const& s)->std::string {
    std::stringstream ss;
    ss << std::quoted(s);
    return ss.str();
  });
  String script = fmt::format(R"(
local tb = ctx:arg("{}"):asInt()
local menu = {{ {} }}
if ctx:inputData(0) and ctx:inputData(0):numTables()>0 and tb<ctx:inputData(0):numTables() then
  for _,v in ipairs(ctx:inputData(0):table(tb):columns()) do
    table.insert(menu, v)
  end
end
self:desc():setMenu(menu)
)", table, fmt::join(quotedMenu, ", "));

  return ArgDescBuilder(argname)
    .label(label.empty() ? argname : label)
    .type(ArgType::MENU)
    .description(description)
    .updateScript(script);
}
// }}}

/// Functor should have signature like `void(*)(sint row, CellIndex idx, bool conditionMatched)`
template <class Functor>
void filter(String const& conditionExpr, DataTable* intable, Functor f)
{
  PROFILER_SCOPE_DEFAULT();
  std::smatch match;
  // test here: https://regexr.com/ or here: https://regex101.com/
  if (std::regex_match(conditionExpr, match, std::regex(R"#(^\s*\$\{(([^{}]+)(\.([xyzw]))|[^{}]+)\}\s*(==?|>=?|<=?|!=)\s*([^\s]*)\s*$)#"))) {
    // e.g.
    // ${column}==12
    // ${column}>=14
    // ${column}<0.12
    // ${column.x}<0.12
    ASSERT(match.size() == 7);
    bool const hasComponent = match[3].matched;

    static int const componentIndexMap[] = { 3,0,1,2 };
    String const columnName = hasComponent ? match[2] : match[1];
    int    const component = hasComponent && match[4].length()==1 ? componentIndexMap[match[4].str()[0]-'w'] : 0;
    String const compareOp = match[5];
    String const targetVal = match[6];

    enum Compare {
      EQ, NE, LT, LE, GT, GE
    };
    static const std::unordered_map<String, Compare> compareOpMap = {
      {"==", EQ},
      {"=",  EQ},
      {"!=", NE},
      {"<",  LT},
      {"<=", LE},
      {">",  GT},
      {">=", GE},
    };
    Compare cmp = compareOpMap.at(compareOp);

    auto* column = intable->getColumn(columnName);
    RUNTIME_CHECK(column, "column \"{}\" does not exist", columnName, component);

#define NUMERIC_COMPARE(TYPE) \
    auto const ts=column->tupleSize();                                                           \
    size_t const numidx = intable->numIndices();                                                 \
    auto arr=static_cast<TYPE const*>(column->asNumericData()->getRawBufferRO(0, numidx*ts, dt)); \
    for (sint row=0, numrow=intable->numRows(); row<numrow; ++row) {  \
      auto idx  = intable->getIndex(row);                             \
      auto val  = arr[idx.value()*ts+component];                      \
      bool pass = false;               \
      switch (cmp) {                   \
      case EQ:                         \
        pass = val == tval; break;     \
      case NE:                         \
        pass = val != tval; break;     \
      case LT:                         \
        pass = val < tval; break;      \
      case LE:                         \
        pass = val <= tval; break;     \
      case GT:                         \
        pass = val > tval; break;      \
      case GE:                         \
        pass = val >= tval; break;     \
      default:                         \
        ASSERT(!"unknown compare op"); \
      }                                \
      f(row, idx, pass);               \
    }
    if (column->asNumericData()) {
      RUNTIME_CHECK(!(!column->asNumericData() && hasComponent),
          "column \"{}\" has no component \"{}\"", columnName, component);
      auto const dt = column->dataType();
      char* pEnd = nullptr;
      switch(dt) {
      case DataType::INT32: {
        int32_t tval = strtol(targetVal.c_str(), &pEnd, 10);
        NUMERIC_COMPARE(int32_t);
        break;
      }
      case DataType::INT64: {
        int64_t tval = strtoll(targetVal.c_str(), &pEnd, 10);
        NUMERIC_COMPARE(int64_t);
        break;
      }
      case DataType::UINT32: {
        uint32_t tval = strtoul(targetVal.c_str(), &pEnd, 10);
        NUMERIC_COMPARE(uint32_t);
        break;
      }
      case DataType::UINT64: {
        uint64_t tval = strtoull(targetVal.c_str(), &pEnd, 10);
        NUMERIC_COMPARE(uint64_t);
        break;
      }
      case DataType::FLOAT: {
        float tval = strtof(targetVal.c_str(), &pEnd);
        NUMERIC_COMPARE(float);
        break;
      }
      case DataType::DOUBLE: {
        double tval = strtod(targetVal.c_str(), &pEnd);
        NUMERIC_COMPARE(double);
        break;
      }
      default:
        throw TypeError(fmt::format("illegal operation \"{}\" on column \"{}\"", compareOp, columnName));
      }
#undef NUMERIC_COMPARE
    } else if (column->asStringData()) {
      for (sint row=0, numrow=intable->numRows(); row<numrow; ++row) {
        auto idx = intable->getIndex(row);
        bool pass = false;
        switch (cmp) {
        case EQ:
          pass = column->get<StringView>(idx) == targetVal; break;
        case NE:
          pass = !(column->get<StringView>(idx) == targetVal); break;
        default: {
          RUNTIME_CHECK(false, "illegal operation \"{}\" on string column \"{}\"", compareOp, columnName);
        }
        }
        f(row, idx, pass);
      }
    } else { // TODO: more filters?
      for (sint row = 0, numrow = intable->numRows(); row < numrow; ++row) {
        auto idx = intable->getIndex(row);
        f(row, idx, true);
      }
    }
  } else if (std::regex_match(conditionExpr, match, std::regex(R"#(^\s*\$\{([^{}]+)\}\s*~=/(.+)/$)#"))) {
    // string regex matching
    ALWAYS_ASSERT(match.size()==3);
    String columnName = match[1];
    String pattern = match[2];
    std::regex re(pattern);
    auto* column = intable->getColumn(columnName);
    ALWAYS_ASSERT(column != nullptr);
    ALWAYS_ASSERT(column->asStringData());
    for (sint row = 0, numrow = intable->numRows(); row < numrow; ++row) {
      auto idx = intable->getIndex(row);
      auto sv = column->get<StringView>(idx);
      f(row, idx, std::regex_match(sv.begin(), sv.end(), re));
    }
  } else { // TODO: more filters
    for (sint row = 0, numrow = intable->numRows(); row < numrow; ++row) {
      auto idx = intable->getIndex(row);
      f(row, idx, true);
    }
  }
}

}

END_JOYFLOW_NAMESPACE

