#include "def.h"
#include "datatable.h"
#include "opcontext.h"
#include "opgraph.h"
#include "oparg.h"
#include "profiler.h"

#include <spdlog/spdlog.h>
#include <glm/glm.hpp>
#include <sol/sol.hpp>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

BEGIN_JOYFLOW_NAMESPACE


static int lua_DataTable_result(lua_State* L, DataTable* table, String const& column, int row)
{
  auto* pcolumn = table->getColumn(column);
  if (!pcolumn)
    return 0;

  CellIndex ci = table->getIndex(row);
  if (!ci.valid())
    return 0;

  if (pcolumn->asNumericData()) {
    for (int ti = 0; ti < pcolumn->tupleSize(); ++ti) {
      if (pcolumn->dataType()==DataType::DOUBLE||pcolumn->dataType()==DataType::FLOAT)
        sol::stack::push(L, pcolumn->get<double>(ci, ti));
      else
        sol::stack::push(L, pcolumn->get<sint>(ci, ti));
    }
    return int(pcolumn->tupleSize());
  } else if (pcolumn->asStringData()) {
    sol::stack::push(L, pcolumn->get<StringView>(ci));
    return 1;
  } else if (auto* vi=pcolumn->asVectorData()) {
    RUNTIME_CHECK(pcolumn->tupleSize() == 1, "array of tuples are not supported in lua yet");
    switch (pcolumn->dataType()) {
    case DataType::INT32: {
      auto *vec = vi->asVector<int32_t>(ci);
      RUNTIME_CHECK(vec, "cannot get {}[{}] as vector", column, ci.value());
      sol::stack::push(L, sol::as_table(*vec));
      break;
    }
    case DataType::INT64: {
      auto *vec = vi->asVector<int64_t>(ci);
      RUNTIME_CHECK(vec, "cannot get {}[{}] as vector", column, ci.value());
      sol::stack::push(L, sol::as_table(*vec));
      break;
    }
    case DataType::UINT32: {
      auto *vec = vi->asVector<uint32_t>(ci);
      RUNTIME_CHECK(vec, "cannot get {}[{}] as vector", column, ci.value());
      sol::stack::push(L, sol::as_table(*vec));
      break;
    }
    case DataType::UINT64: {
      auto *vec = vi->asVector<uint64_t>(ci);
      RUNTIME_CHECK(vec, "cannot get {}[{}] as vector", column, ci.value());
      sol::stack::push(L, sol::as_table(*vec));
      break;
    }
    case DataType::FLOAT: {
      auto *vec = vi->asVector<float>(ci);
      RUNTIME_CHECK(vec, "cannot get {}[{}] as vector", column, ci.value());
      sol::stack::push(L, sol::as_table(*vec));
      break;
    }
    case DataType::DOUBLE: {
      auto *vec = vi->asVector<double>(ci);
      RUNTIME_CHECK(vec, "cannot get {}[{}] as vector", column, ci.value());
      sol::stack::push(L, sol::as_table(*vec));
      break;
    }
    default:
      throw TypeError(fmt::format("cannot get vector<{}> data to lua", dataTypeName(pcolumn->dataType())));
    } // switch type
    return 1;
  } else {
    throw TypeError(fmt::format("don't know how to get value from {} of type {}", column, dataTypeName(pcolumn->dataType())));
  }
  return 0;
}

static int lua_DataCollection_get(lua_State* L)
{
  DataCollection* collection = sol::stack::get<DataCollection*>(L, 1);

  int        tabid  = sol::stack::get<int>(L, 2);
  String     column = sol::stack::get<const char*>(L, 3);
  int        row    = sol::stack::get<int>(L, 4);
  DataTable* table  = collection->getTable(tabid);

  if (!table)
    return 0;

  return lua_DataTable_result(L, table, column, row);
}

static int lua_DataTable_get(lua_State* L)
{
  DataTable* table = sol::stack::get<DataTable*>(L, 1);
  RUNTIME_CHECK(table, "DataTable was nil");
  if (lua_gettop(L) == 2) { // 2 arguments : DataTable(self), string(key) -> getVariable
    String key = sol::stack::get<const char*>(L, 2);
    auto val = table->getVariable(key);
    if (!val.has_value())
      return 0;
    auto const& tp = val.type();
    if (tp == typeid(uint8_t)) {
      lua_pushinteger(L, std::any_cast<uint8_t>(val));
    } else if (tp == typeid(int8_t)) {
      lua_pushinteger(L, std::any_cast<int8_t>(val));
    } else if (tp == typeid(uint16_t)) {
      lua_pushinteger(L, std::any_cast<uint16_t>(val));
    } else if (tp == typeid(int16_t)) {
      lua_pushinteger(L, std::any_cast<int16_t>(val));
    } else if (tp == typeid(uint32_t)) {
      lua_pushinteger(L, std::any_cast<uint32_t>(val));
    } else if (tp == typeid(int32_t)) {
      lua_pushinteger(L, std::any_cast<int32_t>(val));
    } else if (tp == typeid(uint64_t)) {
      lua_pushinteger(L, std::any_cast<uint64_t>(val));
    } else if (tp == typeid(int64_t)) {
      lua_pushinteger(L, std::any_cast<int64_t>(val));
    } else if (tp == typeid(float)) {
      lua_pushnumber(L, std::any_cast<float>(val));
    } else if (tp == typeid(double)) {
      lua_pushnumber(L, std::any_cast<double>(val));
    } else if (tp == typeid(bool)) {
      lua_pushboolean(L, std::any_cast<bool>(val));
    } else if (tp == typeid(joyflow::String)) {
      lua_pushstring(L, std::any_cast<joyflow::String>(val).c_str());
    } else if (tp == typeid(std::string_view)) {
      lua_pushstring(L, std::any_cast<std::string_view>(val).data());
    } else {
      throw Unimplemented(fmt::format("Don\'t know how to get {}", key));
    }
    return 1;
  }

  // else -> get column data
  String    column = sol::stack::get<const char*>(L, 2);
  int       row    = sol::stack::get<int>(L, 3);

  return lua_DataTable_result(L, table, column, row);
}

static bool importLuaValue(DataColumn* col, CellIndex ci, lua_State* L, int stk)
{
  col->makeUnique();
  bool succeed = true;
  if (isNumeric(col->dataType()) && col->tupleSize() == 1 && !col->desc().container) {
    switch (col->dataType()) {
    case DataType::INT32: case DataType::INT64:
    case DataType::UINT32: case DataType::UINT64:
      col->set<int64_t>(ci, luaL_checkinteger(L, stk));
      break;
    case DataType::FLOAT: case DataType::DOUBLE:
      col->set<double>(ci, luaL_checknumber(L, stk));
      break;
    default:
      succeed = false;
    }
  } else if (isNumeric(col->dataType())) {
    if (lua_istable(L, stk)) {
      auto ltb = sol::stack::get<sol::table>(L, stk);
      if (!col->desc().container) {
        RUNTIME_CHECK(sint(ltb.size()) <= col->tupleSize(), "tuple too big to fit in column {}", col->name());
        switch (col->dataType()) {
        case DataType::INT32: case DataType::INT64:
        case DataType::UINT32: case DataType::UINT64: {
          for (int i = 0; i < ltb.size(); ++i) {
            col->set<int64_t>(ci, ltb.get<int64_t>(i + 1), i);
          }
          break;
        }
        case DataType::FLOAT: case DataType::DOUBLE: {
          for (int i = 0; i < ltb.size(); ++i) {
            col->set<double>(ci, ltb.get<double>(i + 1), i);
          }
          break;
        }
        default:
          succeed = false;
        }
      } else { // is a vector column
        if (col->desc().tupleSize != 1) {
          throw Unimplemented("array of tuples are not supported yet");
        }
        auto* vi = col->asVectorData();
        RUNTIME_CHECK(vi, "vector interface of column {} was missing", col->name());
        switch (col->dataType()) {
        case DataType::INT32: {
          auto* vec = vi->asVector<int32_t>(ci);
          RUNTIME_CHECK(vec, "cannot get {}[{}] as vector", col->name(), ci.value());
          *vec = ltb.as<Vector<int32_t>>();
          break;
        }
        case DataType::INT64: {
          auto* vec = vi->asVector<int64_t>(ci);
          RUNTIME_CHECK(vec, "cannot get {}[{}] as vector", col->name(), ci.value());
          *vec = ltb.as<Vector<int64_t>>();
          break;
        }
        case DataType::UINT32: {
          auto* vec = vi->asVector<uint32_t>(ci);
          RUNTIME_CHECK(vec, "cannot get {}[{}] as vector", col->name(), ci.value());
          *vec = ltb.as<Vector<uint32_t>>();
          break;
        }
        case DataType::UINT64: {
          auto* vec = vi->asVector<uint64_t>(ci);
          RUNTIME_CHECK(vec, "cannot get {}[{}] as vector", col->name(), ci.value());
          *vec = ltb.as<Vector<uint64_t>>();
          break;
        }
        case DataType::FLOAT: {
          auto* vec = vi->asVector<float>(ci);
          RUNTIME_CHECK(vec, "cannot get {}[{}] as vector", col->name(), ci.value());
          *vec = ltb.as<Vector<float>>();
          break;
        }
        case DataType::DOUBLE: {
          auto* vec = vi->asVector<double>(ci);
          RUNTIME_CHECK(vec, "cannot get {}[{}] as vector", col->name(), ci.value());
          *vec = ltb.as<Vector<double>>();
          break;
        }
        default:
          throw TypeError(fmt::format("bad type for assigning column {} of type vector<{}>", col->name(), dataTypeName(col->dataType())));
        }
      }
    } else if (sol::stack::check<vec2>(L, stk)) {
      col->set<vec2>(ci, sol::stack::get<vec2>(L, stk));
    } else if (sol::stack::check<vec3>(L, stk)) {
      col->set<vec3>(ci, sol::stack::get<vec3>(L, stk));
    } else if (sol::stack::check<vec4>(L, stk)) {
      col->set<vec4>(ci, sol::stack::get<vec4>(L, stk));
    } else if (sol::stack::check<ivec2>(L, stk)) {
      col->set<ivec2>(ci, sol::stack::get<ivec2>(L, stk));
    } else if (sol::stack::check<ivec3>(L, stk)) {
      col->set<ivec3>(ci, sol::stack::get<ivec3>(L, stk));
    } else if (sol::stack::check<ivec4>(L, stk)) {
      col->set<ivec4>(ci, sol::stack::get<ivec4>(L, stk));
    } else {
      throw TypeError(fmt::format("bad value type to set column \"{}\"", col->name()));
    }
  } else if (col->dataType() == DataType::STRING) {
    col->set(ci, sol::stack::get<StringView>(L, stk));
  } else {
    succeed = false;
  }
  return succeed;
}

/// syntax:
///   table:set('var', value) -- set variable
///   table:set('col', row, value) -- set value of column 'col', row 'row',
///                                   value can be int/double/vec2/vec3/vec4/ivec2/ivec3/ivec4/string/array of numbers
///    raises error if value type does not match column type
static int lua_DataTable_set(lua_State* L)
{
  DataTable* table = sol::stack::get<DataTable*>(L, 1);
  if (!table) {
    spdlog::warn("table is nil");
    return 0;
  }
  table->makeUnique();
  if (lua_gettop(L)==3) { // only 3 arguments : table(self), key, value -> set variable
    const char* key = sol::stack::get<const char*>(L, 2);
    if (table->getColumn(key)) {
      spdlog::warn("setting variable \"{0}\", while column \"{0}\" also exists, please be sure", key);
    }
    if (lua_isnil(L, 3)) {
      table->setVariable(key, std::any{}); // erases existing variable
    } else if (lua_isboolean(L, 3)) {
      table->setVariable(key, sol::stack::get<bool>(L, 3));
    } else if (lua_isinteger(L, 3)) {
      table->setVariable(key, sol::stack::get<sint>(L, 3));
    } else if (lua_isnumber(L, 3)) {
      table->setVariable(key, sol::stack::get<real>(L, 3));
    } else if (lua_isstring(L, 3)) {
      table->setVariable(key, sol::stack::get<StringView>(L, 3));
    } else {
      throw Unimplemented(fmt::format("Don\'t know how to set {}", key));
      return 0;
    }

    lua_pushboolean(L, true);
    return 1;
  }

  // else -> set data column
  String      colname = "";
  DataColumn* col     = nullptr;
  sint        row     = -1;
  CellIndex   ci;


  if (sol::stack::check<const char*>(L, 2)) {
    colname = sol::stack::get<const char*>(L, 2);
    col = table->getColumn(colname);
  } else if (sol::stack::check<DataColumn*>(L, 2)) {
    col = sol::stack::get<DataColumn*>(L, 2);
    colname = col->name();
  }

  if (!col) {
    spdlog::warn("column {} does not exist", colname);
    return 0;
  }
  if (lua_isinteger(L, 3)) {
    row = luaL_checkinteger(L, 3);
    ci  = table->getIndex(row);
  } else if (sol::stack::check<CellIndex>(L, 3)) {
    ci = sol::stack::get<CellIndex>(L, 3);
    row = table->getRow(ci);
  } else {
    throw TypeError("DataTable::set takes integer or CellIndex as 2nd argument");
  }
  if (!ci.valid()) {
    spdlog::warn("row {} is invalid", row);
    return 0;
  }

  col->makeUnique();
  bool succeed = false;
  if (isNumeric(col->dataType()) && !col->desc().container) {
    if (lua_gettop(L) == 5 && lua_isinteger(L, 5)) {
      sint tupleidx = luaL_checkinteger(L, 5);
      RUNTIME_CHECK(tupleidx>=0 && tupleidx<col->tupleSize(),
          "the tupleIndex argument for column {} should always be in range [0, {})",
          colname, col->tupleSize());
      if (col->dataType() == DataType::FLOAT || col->dataType() == DataType::DOUBLE) {
        col->set<double>(ci, luaL_checknumber(L, 4), tupleidx);
      } else {
        col->set<sint>(ci, luaL_checkinteger(L, 4), tupleidx);
      }
      succeed = true;
    }
  }
  if (!succeed)
    succeed = importLuaValue(col, ci, L, 4);

  RUNTIME_CHECK(succeed, "lua does not know how to deal with datatype {}", dataTypeName(col->dataType()));
  sol::stack::push(L, succeed);
  return 1;
}

/// syntax:
///   table:addColumn('int_column', 0) -- int column named 'int_column', with default value being 0
///   table:addColumn('flt_column', 0.0) -- floating point (double) column named 'flt_column', with default value being 0.0
///   table:addColumn('str_column', '') -- string column named 'str_column', with default value being ''
///   table:addColumn('vec_column', vec3:new(1,2,3)) -- vec3 column named 'vec_column', with default value being (1,2,3)
///   table:addColumn('arr_column', {'int32'}) -- column named 'arr_column' which stores array of int32s
///                                               possible types are int32/int64/float/double
/// each of the above overrides can take another bool argument, overwrite existing column or not
static int lua_DataTable_addColumn(lua_State *L)
{
  DataTable*  table = sol::stack::get<DataTable*>(L, 1);
  String      name = sol::stack::get<const char*>(L, 2);
  DataColumn* column = nullptr;
  bool        overwrite = false;
  if (sol::stack::top(L) > 3)
    overwrite = sol::stack::get<bool>(L, 4);

  if (!table || name.empty())
    return 0;

  if (lua_isinteger(L, 3)) {
    // int column
    column = table->createColumn(name, sol::stack::get<int>(L, 3), overwrite);
  } else if (lua_isnumber(L, 3)) {
    // double column
    column = table->createColumn(name, sol::stack::get<double>(L, 3), overwrite);
  } else if (lua_isstring(L, 3)) {
    // string column
    column = table->createColumn<String>(name, sol::stack::get<String>(L, 3), overwrite);
  } else if (sol::stack::check<vec2>(L, 3)) {
    column = table->createColumn<vec2>(name, sol::stack::get<vec2>(L, 3), overwrite);
  } else if (sol::stack::check<vec3>(L, 3)) {
    column = table->createColumn<vec3>(name, sol::stack::get<vec3>(L, 3), overwrite);
  } else if (sol::stack::check<vec4>(L, 3)) {
    column = table->createColumn<vec4>(name, sol::stack::get<vec4>(L, 3), overwrite);
  } else if (sol::stack::check<ivec2>(L, 3)) {
    column = table->createColumn<ivec2>(name, sol::stack::get<ivec2>(L, 3), overwrite);
  } else if (sol::stack::check<ivec3>(L, 3)) {
    column = table->createColumn<ivec3>(name, sol::stack::get<ivec3>(L, 3), overwrite);
  } else if (sol::stack::check<ivec4>(L, 3)) {
    column = table->createColumn<ivec4>(name, sol::stack::get<ivec4>(L, 3), overwrite);
  } else if (lua_istable(L, 3)) { // usage: addColumn('weights', {'int'})
    auto ltb = sol::stack::get<sol::table>(L, 3);
    RUNTIME_CHECK(ltb.size()==1, "syntax for adding vector column: addColumn('name', {{'type'}})");
    auto type = ltb.get<String>(1);
    if (type=="int32" || type=="int") {
      column = table->createColumn<Vector<int32_t>>(name, overwrite);
    } else if (type=="int64") {
      column = table->createColumn<Vector<int64_t>>(name, overwrite);
    } else if (type=="float") {
      column = table->createColumn<Vector<float>>(name, overwrite);
    } else if (type=="double" || type=="real") {
      column = table->createColumn<Vector<double>>(name, overwrite);
    } else {
      throw Unimplemented(fmt::format("type for vector column should be one of (int|int32|int64|float|double|real), got {}", type));
    }
  } else {
    auto ltype = lua_typename(L, lua_type(L, 3));
    throw Unimplemented(fmt::format("don't know how to create column \"{}\" of type \"{}\"", name, ltype));
  }

  if (column) {
    sol::stack::push(L, column);
    return 1;
  } else {
    return 0;
  }
}

struct TableRowAccessor
{
  DataTable* table;
  CellIndex  cindex;

  static int __index(lua_State* L)
  {
    auto self = sol::stack::get<TableRowAccessor>(L, 1);
    DataColumn *col = nullptr;
    String  colname = "";
    if (sol::stack::check<DataColumn*>(L, 2)) {
      col = sol::stack::get<DataColumn*>(L, 2);
      colname = col->name();
    } else if (sol::stack::check<const char*>(L, 2)) {
      colname = sol::stack::get<const char*>(L, 2);
      col = self.table->getColumn(colname);
    }
    RUNTIME_CHECK(col != nullptr, "column \"{}\" cannot be found", colname);
    if (!self.cindex.valid()) {
      spdlog::warn("accessing invalid row");
    } else {
      if (col->asNumericData()) {
        if (col->dataType() == DataType::FLOAT || col->dataType() == DataType::DOUBLE) {
          for (int ti = 0; ti < col->tupleSize(); ++ti) {
            sol::stack::push(L, col->get<double>(self.cindex, ti));
          }
        } else {
          for (int ti = 0; ti < col->tupleSize(); ++ti) {
            sol::stack::push(L, col->get<sint>(self.cindex, ti));
          }
        }
        return int(col->tupleSize());
      } else if (col->asStringData()) {
        sol::stack::push(L, col->get<StringView>(self.cindex));
        return 1;
      }
    }
    return 0;
  }

  static int __newindex(lua_State* L)
  {
    auto self = sol::stack::get<TableRowAccessor>(L, 1);
    DataColumn *col = nullptr;
    String  colname = "";
    if (sol::stack::check<DataColumn*>(L, 2)) {
      col = sol::stack::get<DataColumn*>(L, 2);
      colname = col->name();
    } else if (sol::stack::check<const char*>(L, 2)) {
      colname = sol::stack::get<const char*>(L, 2);
      col = self.table->getColumn(colname);
    }
    // create an new column if column does not exist yet
    if (col == nullptr && lua_gettop(L) == 3) {
      sol::stack::push(L, lua_DataTable_addColumn);
      sol::stack::push(L, self.table);
      sol::stack::push(L, colname);
      lua_pushvalue(L, 3);
      lua_call(L, 3, 1);
      col = sol::stack::get<DataColumn*>(L, -1);
    }

    RUNTIME_CHECK(col != nullptr, "column \"{}\" cannot be found", colname);
    RUNTIME_CHECK(self.cindex.valid(), "assigning invalid index");

    bool succeed = importLuaValue(col, self.cindex, L, 3);
    RUNTIME_CHECK(succeed, "lua does not know how to deal with datatype {}", dataTypeName(col->dataType()));
    return 0;
  }

  static TableRowAccessor create(DataTable* table, sint row)
  {
    return TableRowAccessor{ table, table->getIndex(row) };
  }
};

/// syntax: table:foreach(function(row) row.xxxx = row.yyy end)
static int lua_DataTable_foreach(lua_State* L)
{
  auto* dt = sol::stack::get<DataTable*>(L, 1);
  auto lfunc = sol::stack::get<sol::unsafe_function>(L, 2);
  for (sint i=0, n=dt->numRows(); i<n; ++i) {
    lfunc(TableRowAccessor::create(dt, i));
  }
  return 0;
}

CORE_API void bindLuaTypes(lua_State* L, bool readonly)
{
  PROFILER_SCOPE("bind types", 0xE3F9FD);
  sol::state_view lua(L);
  lua.open_libraries(sol::lib::math, sol::lib::base, sol::lib::table, sol::lib::string, sol::lib::utf8, sol::lib::bit32);
  lua["newGraph"] = newGraph;
  lua["deleteGraph"] = deleteGraph;
  lua["info"] = [](std::string const& str) {
    spdlog::info("lua message: {}", str);
  };
  lua.new_usertype<vec2>(
    "vec2", sol::constructors<vec2(), vec2(real), vec2(real, real)>(),
    "x",    &vec2::x,
    "y",    &vec2::y
  );
  lua.new_usertype<vec3>(
    "vec3", sol::constructors<vec3(), vec3(real), vec3(vec2, real), vec3(real,real,real)>(),
    "x",    &vec3::x,
    "y",    &vec3::y,
    "z",    &vec3::z
  );
  lua.new_usertype<vec4>(
    "vec4", sol::constructors<vec4(), vec4(real), vec4(vec3, real), vec4(real,real,real,real)>(),
    "x",    &vec4::x,
    "y",    &vec4::y,
    "z",    &vec4::z,
    "w",    &vec4::w
  );

  lua.new_usertype<ivec2>(
    "ivec2", sol::constructors<ivec2(), ivec2(int), ivec2(int, int)>(),
    "x",    &ivec2::x,
    "y",    &ivec2::y
  );
  lua.new_usertype<ivec3>(
    "ivec3", sol::constructors<ivec3(), ivec3(int), ivec3(ivec2, int), ivec3(int,int,int)>(),
    "x",    &ivec3::x,
    "y",    &ivec3::y,
    "z",    &ivec3::z
  );
  lua.new_usertype<ivec4>(
    "ivec4", sol::constructors<ivec4(), ivec4(int), ivec4(ivec3, int), ivec4(int,int,int,int)>(),
    "x",    &ivec4::x,
    "y",    &ivec4::y,
    "z",    &ivec4::z,
    "w",    &ivec4::w
  );
  lua.new_usertype<OpNode>(
    "Node", sol::no_constructor,
    "name", &OpNode::name
  );
  auto luaopgraph = lua.new_usertype<OpGraph>(
    "Graph",    sol::no_constructor,
    "name",     &OpGraph::name,
    "node",     &OpGraph::node,
    "children", &OpGraph::childNames
  );
  if (!readonly) {
    luaopgraph.set_function("addNode",    &OpGraph::addNode);
    luaopgraph.set_function("removeNode", &OpGraph::removeNode);
    luaopgraph.set_function("link",       static_cast<bool(OpGraph::*)(String const&, sint, String const&, sint)>(&OpGraph::link));
    luaopgraph.set_function("unlink",     static_cast<bool(OpGraph::*)(String const&, sint, String const&, sint)>(&OpGraph::unlink));
  }
  auto luadatatable = lua.new_usertype<DataTable>(
    "DataTable",  sol::no_constructor,
    "get",        lua_DataTable_get,
    "column",     static_cast<DataColumn*(DataTable::*)(String const&)>(&DataTable::getColumn),
    "columns",    &DataTable::columnNames,
    "numRows",    &DataTable::numRows,
    "numIndices", &DataTable::numIndices,
    "__length",   &DataTable::numRows
    // "__index",    &TableRowAccessor::create
  );
  if (!readonly) {
    luadatatable.set_function("makeUnique",   &DataTable::makeUnique);
    luadatatable.set_function("set",          lua_DataTable_set);
    luadatatable.set_function("addColumn",    lua_DataTable_addColumn);
    luadatatable.set_function("renameColumn", &DataTable::renameColumn);
    luadatatable.set_function("removeColumn", &DataTable::removeColumn);
    luadatatable.set_function("addRow",       &DataTable::addRow);
    luadatatable.set_function("addRows",      &DataTable::addRows);
    luadatatable.set_function("removeRow",    &DataTable::removeRow);
    luadatatable.set_function("removeRows",   &DataTable::removeRows);
    luadatatable.set_function("foreach",      lua_DataTable_foreach);
  }

  auto luatableaccessor = lua.new_usertype<TableRowAccessor>(
    "RowAccessor", sol::no_constructor,
    "__index", &TableRowAccessor::__index,
    "__newindex", &TableRowAccessor::__newindex
  );
  auto luadatacollection = lua.new_usertype<DataCollection>(
    "DataCollection", sol::no_constructor,
    "numTables",      &DataCollection::numTables,
    "table",          static_cast<DataTable* (DataCollection::*)(sint)>(&DataCollection::getTable),
    "get",            lua_DataCollection_get,
    "__index",        static_cast<DataTable* (DataCollection::*)(sint)>(&DataCollection::getTable)
  );
  if (!readonly) {
    luadatacollection.set_function("addTable", static_cast<sint (DataCollection::*)()>(&DataCollection::addTable));
  }

  lua.new_usertype<ArgDesc>(
    "ArgDesc", sol::no_constructor,
    "valueRange", &ArgDesc::valueRange,
    "menu",       &ArgDesc::menu,
    "setValueRange", [](ArgDesc& desc, real a, real b) { desc.valueRange[0] = a; desc.valueRange[1] = b; },
    "setMenu",       [](ArgDesc& desc, sol::as_table_t<std::vector<std::string>> const& m) { desc.menu.assign(m.value().begin(), m.value().end()); }
  );
  lua.new_usertype<ArgValue>(
    "ArgValue", sol::no_constructor,
    "asString", &ArgValue::asString,
    "asInt",    &ArgValue::asInt,
    "asInt2",   &ArgValue::asInt2,
    "asInt3",   &ArgValue::asInt3,
    "asInt4",   &ArgValue::asInt4,
    "asReal",   &ArgValue::asReal,
    "asReal2",  &ArgValue::asReal2,
    "asReal3",  &ArgValue::asReal3,
    "asReal4",  &ArgValue::asReal4,
    "desc",     &ArgValue::mutDesc
  );
  lua.new_usertype<OpContext>(
    "Context",   sol::no_constructor,
    "arg",       [](OpContext* ctx, String const& name) { return &ctx->arg(name); },
    "inputData", [](OpContext* ctx, int pin) { return ctx->fetchInputData(pin); }
  );
}

END_JOYFLOW_NAMESPACE

