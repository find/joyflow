#include "oparg.h"
#include "opcontext.h"
#include "runtime.h"
#include "serialize.h"
#include "profiler.h"

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <glm/glm.hpp>
#include <sol/sol.hpp>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <algorithm> //min/max
#include <cstring>   //memset

BEGIN_JOYFLOW_NAMESPACE

CORE_API void ArgValue::eval(OpContext* context)
{
  PROFILER_SCOPE("ArgValue::eval", 0xE29C45);
  bool        valueDirty = false;
  lua_State*  L          = nullptr;
  auto const& desc       = this->desc();
  ensureVectorSize(expr_,                  desc.tupleSize);
  ensureVectorSize(evaluatedStringValues_, desc.tupleSize);

  if (desc.type==ArgType::MULTI_MENU || desc.type==ArgType::MENU) { // no expression allowed
    evaluatedStringValues_.resize(desc.tupleSize);
    for (sint i = 0; i < desc.tupleSize; ++i) {
      if (expr_[i] != evaluatedStringValues_[i])
        valueDirty = true;
      evaluatedStringValues_[i] = expr_[i];
    }
    if (desc.type == ArgType::MENU) {
      auto itr = std::find(desc.menu.begin(), desc.menu.end(), expr_[0]);
      if (itr != desc.menu.end() && itr - desc.menu.begin() != evaluatedIntValues_[0]) {
        evaluatedIntValues_[0] = itr - desc.menu.begin();
        valueDirty = true;
      }
    }
  } else {
    for (sint i = 0; i < std::min(MAX_ARG_TUPLE_SIZE, desc.tupleSize); ++i) {
      if (!isExpr_[i])
        continue;
      isValid_[i] = true;
      String expr = expr_[i];
      auto   startquote = expr.find('`');
      while (startquote != String::npos) {
        auto endquote = expr.find('`', startquote + 1);
        if (endquote == String::npos) {
          errorMessage_ = "quote was not closed";
          endquote = startquote;
          break;
        }
        auto quoted = expr.substr(startquote + 1, endquote - startquote - 1);
        if (!quoted.empty()) {
          bool newlyCreated = false;
          if (L == nullptr) {
            L = luaL_newstate();
            newlyCreated = true;
          }
          auto view = sol::state_view(L);
          if (newlyCreated)
            view.open_libraries(sol::lib::base,
              sol::lib::string,
              sol::lib::table,
              sol::lib::math,
              sol::lib::bit32,
              sol::lib::utf8);
          String expanded = "";
          spdlog::info("Arg: evaluating \"{}\"", quoted);
          try {
            auto result =
              view.safe_script("return tostring(" + quoted + ")", sol::script_throw_on_error);
            expanded = result.get<String>();
          } catch (sol::error const& err) {
            errorMessage_ = err.what();
            isValid_[i] = false;
          }
          spdlog::info("Arg: got \"{}\"", expanded);
          expr = expr.substr(0, startquote) + expanded + expr.substr(endquote + 1);
        }
        startquote = expr.find('`');
      }
      if ((desc.type == ArgType::STRING || desc.type == ArgType::CODEBLOCK || desc.type == ArgType::OPREF)
          && evaluatedStringValues_[i] != expr)
        valueDirty = true;
      evaluatedStringValues_[i] = expr;

      if (desc.type == ArgType::INT) {
        auto v = toInt(evaluatedStringValues_[i]);
        if (desc.closeRange[0])
          v = std::max(v, sint(desc.valueRange[0]));
        if (desc.closeRange[1])
          v = std::min(v, sint(desc.valueRange[1]));
        if (v != evaluatedIntValues_[i])
          valueDirty = true;
        evaluatedIntValues_[i] = v;
        evaluatedRealValues_[i] = real(v);
        evaluatedStringValues_[i] = std::to_string(v);
      } else if (desc.type == ArgType::BOOL || desc.type == ArgType::TOGGLE) {
        String lowerexpr = expr;
        std::transform(expr.begin(), expr.end(), lowerexpr.begin(), [](char c) {return c >= 'A'&&c <= 'Z' ? c - 'A' + 'a' : c; });
        bool b = lowerexpr == "true" || lowerexpr == "1";
        sint v = b;
        if (v != evaluatedIntValues_[i])
          valueDirty = true;
        evaluatedIntValues_[i] = v;
        evaluatedRealValues_[i] = real(v);
        evaluatedStringValues_[i] = b ? "true" : "false";
      } if (desc.type == ArgType::REAL) {
        auto v = toReal(evaluatedStringValues_[i]);
        if (desc.closeRange[0])
          v = std::max(v, desc.valueRange[0]);
        if (desc.closeRange[1])
          v = std::min(v, desc.valueRange[1]);
        if (v != evaluatedRealValues_[i])
          valueDirty = true;
        evaluatedRealValues_[i] = v;
        evaluatedIntValues_[i] = sint(v);
        evaluatedStringValues_[i] = std::to_string(v);
      } else {
        // pass
        // throw Unimplemented(fmt::format("don't know how to evaluate arg of type {}", desc.type));
      }
    }
  }

  if (L)
    lua_close(L);
  if (valueDirty)
    ++evaluatedVersion_;
}

static void saveArgDescDiff(Json& json, ArgDesc const& own, ArgDesc const* ref)
{
#define SAVE_IF_DIFF(field) \
  if (!ref || own.field != ref->field) json[#field] = own.field

  SAVE_IF_DIFF(type);
  SAVE_IF_DIFF(name);
  SAVE_IF_DIFF(label);
  SAVE_IF_DIFF(tupleSize);
  SAVE_IF_DIFF(description);
  SAVE_IF_DIFF(defaultExpression);
  SAVE_IF_DIFF(valueRange);
  SAVE_IF_DIFF(closeRange);

  bool diffmenu = false;
  if (!ref || own.menu.size() != ref->menu.size())
    diffmenu = true;
  else {
    for (size_t i=0, n=own.menu.size(); i<n; ++i) {
      if (own.menu[i] != ref->menu[i]) {
        diffmenu = true;
        break;
      }
    }
  }
  if (diffmenu)
    json["menu"] = own.menu;

  SAVE_IF_DIFF(updateScript);
  SAVE_IF_DIFF(callback);

#undef SAVE_IF_DIFF
}

static void loadArgDescDiff(Json const& json, ArgDesc& own, ArgDesc const* ref)
{
  if (ref)
    own = *ref;
  else
    own.type = ArgType::STRING; // default to string to hold raw value(s)

#define LOAD_IF_EXIST(field) \
  if (auto itr = json.find(#field); itr != json.end()) \
    itr->get_to(own.field);

  LOAD_IF_EXIST(type);
  LOAD_IF_EXIST(name);
  LOAD_IF_EXIST(label);
  LOAD_IF_EXIST(tupleSize);
  LOAD_IF_EXIST(description);
  LOAD_IF_EXIST(defaultExpression);
  LOAD_IF_EXIST(valueRange);
  LOAD_IF_EXIST(closeRange);
  LOAD_IF_EXIST(menu);
  LOAD_IF_EXIST(updateScript);
  LOAD_IF_EXIST(callback);

#undef LOAD_IF_EXIST
}

bool ArgValue::save(Json& self) const
{
  self["desc"] = {}; // always create this section
  if (ownDesc_ != nullptr) {
    saveArgDescDiff(self["desc"], *ownDesc_, desc_);
  }
  self["expr"] = expr_;
  return true;
}

bool ArgValue::load(Json const& self)
{
  auto descitr = self.find("desc");
  if (descitr != self.end()) {
    loadArgDescDiff(self["desc"], mutDesc(), desc_);
  }
  expr_.clear();
  for (String s : self["expr"])
    expr_.emplace_back(std::move(s));
  std::fill(isExpr_.begin(), isExpr_.end(), true);
  eval(nullptr);
  return true;
}

END_JOYFLOW_NAMESPACE
