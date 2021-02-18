#pragma once

#include "def.h"
#include "error.h"
#include "vector.h"
#include "utility.h"

#include <array>
#include <glm/glm.hpp>

BEGIN_JOYFLOW_NAMESPACE

static constexpr int MAX_ARG_TUPLE_SIZE = 4;

/// List of supported parameter types
enum class ArgType : int
{
  REAL = 0,
  INT,
  BOOL,
  COLOR,
  // TODO: CURVE,
  MENU,
  MULTI_MENU,
  STRING,
  CODEBLOCK,
  DIRPATH,
  FILEPATH_OPEN,
  FILEPATH_SAVE,
  OPREF,
  BUTTON,
  TOGGLE,

  COUNT
};

struct ArgDesc
{
  // decsciptions:
  ArgType        type = ArgType::REAL; //< argument type, also guides UI, @see ArgType
  String         name;                 //< name for referencing
  String         label;                //< name that will apear on UI
  int            tupleSize = 1;        //< number of components for this argument (default 1)
  String         description;          //< a brief explaination
  String         defaultExpression[MAX_ARG_TUPLE_SIZE]; //< default value
  real           valueRange[2] = {0, 1};                //< min-max values (for UI slider)
  bool           closeRange[2] = {false, false};        //< can value exceed min-max range?
  Vector<String> menu;                                  //< int or string arguments may have a menu

  String updateScript; //< script to update params (range / menu ...)
  String callback;     //< callback expression (lua code) (TODO)
                       //  called when
                       //   * button pressed
                       //   * value changed

  String const& fileFilter() const { return defaultExpression[1]; }
  String const& codeLanguage() const { return defaultExpression[1]; }
};

class ArgDescBuilder
{
  ArgDesc desc_;

public:
  ArgDescBuilder(String const& name) { desc_.name = name; }
  ArgDescBuilder& name(String name)
  {
    desc_.name = std::move(name);
    return *this;
  }
  ArgDescBuilder& type(ArgType type)
  {
    desc_.type = type;
    return *this;
  }
  ArgDescBuilder& label(String label)
  {
    desc_.label = std::move(label);
    return *this;
  }
  ArgDescBuilder& tupleSize(int ts)
  {
    desc_.tupleSize = ts;
    return *this;
  }
  ArgDescBuilder& description(String d)
  {
    desc_.description = std::move(d);
    return *this;
  }
  ArgDescBuilder& defaultExpression(int elem, String expr)
  {
    ASSERT(elem >= 0 && elem < MAX_ARG_TUPLE_SIZE);
    desc_.defaultExpression[elem] = std::move(expr);
    return *this;
  }
  /// filter string for FILEPATH_OPEN / FILEPATH_SAVE
  ArgDescBuilder& fileFilter(String filter)
  {
    desc_.defaultExpression[1] = std::move(filter);
    return *this;
  }
  /// language of code block
  ArgDescBuilder& codeLanguage(String lang)
  {
    desc_.defaultExpression[1] = std::move(lang);
    return *this;
  }
  ArgDescBuilder& valueRange(real low, real high)
  {
    desc_.valueRange[0] = low;
    desc_.valueRange[1] = high;
    return *this;
  }
  ArgDescBuilder& closeRange(bool low, bool high)
  {
    desc_.closeRange[0] = low;
    desc_.closeRange[1] = high;
    return *this;
  }
  ArgDescBuilder& menu(Vector<String> items)
  {
    desc_.menu = std::move(items);
    return *this;
  }
  ArgDescBuilder& updateScript(String script)
  {
    desc_.updateScript = std::move(script);
    return *this;
  }
  ArgDescBuilder& callback(String code)
  {
    desc_.callback = std::move(code);
    return *this;
  }

  operator ArgDesc() const { return desc_; }
};

class OpContext;
/// attachment on arguments
/// mainly for UI control
class ArgAttachment
{
public:
  virtual ~ArgAttachment() {}
};

// TODO: each type of argument should have its own implementation
class ArgValue
{
private:
  // link to my description
  ArgDesc const* desc_ = nullptr;
  std::unique_ptr<ArgDesc> ownDesc_;
  std::unique_ptr<ArgAttachment> attachment_;

  // result value:
  sint                                 evaluatedVersion_ = 0;
  Vector<String>                       expr_;
  Vector<String>                       evaluatedStringValues_;
  std::array<bool, MAX_ARG_TUPLE_SIZE> isValid_;
  std::array<bool, MAX_ARG_TUPLE_SIZE> isExpr_;
  std::array<real, MAX_ARG_TUPLE_SIZE> evaluatedRealValues_;
  std::array<sint, MAX_ARG_TUPLE_SIZE> evaluatedIntValues_;
  String                               errorMessage_ = "";
  sint                                 updateScriptEvaluatedVersion_ = -1;

public:
  ArgDesc const& desc() const
  {
    ASSERT(ownDesc_.get() || desc_);
    return ownDesc_ ? *ownDesc_ : *desc_;
  }
  ArgDesc& mutDesc()
  {
    if (!ownDesc_) {
      ownDesc_.reset(new ArgDesc);
      if (desc_)
        *ownDesc_ = *desc_;
    }
    return *ownDesc_;
  }
  bool isDefaultDesc() const { return !ownDesc_; }
  bool asBool() const { return !!evaluatedIntValues_[0]; }

  void setAttachment(ArgAttachment* att) { attachment_.reset(att); }
  ArgAttachment* attachment() { return attachment_.get(); }

  inline sint              asInt() const { return evaluatedIntValues_[0]; }
  inline glm::vec<2, sint> asInt2() const;
  inline glm::vec<3, sint> asInt3() const;
  inline glm::vec<4, sint> asInt4() const;

  inline real              asReal() const { return evaluatedRealValues_[0]; }
  inline glm::vec<2, real> asReal2() const;
  inline glm::vec<3, real> asReal3() const;
  inline glm::vec<4, real> asReal4() const;

  inline String asString() const { return evaluatedStringValues_[0]; }
  inline Vector<String> const& asStringList() const { return evaluatedStringValues_; }

  void setErrorMessage(String msg)
  {
    errorMessage_ = std::move(msg);
  }
  String const& errorMessage() const
  {
    return errorMessage_;
  }
  void setUpdateScriptEvaluatedVersion(sint v)
  {
    updateScriptEvaluatedVersion_ = v;
  }
  sint updateScriptEvaluatedVersion() const
  {
    return updateScriptEvaluatedVersion_;
  }

  String getRawExpr(int elem = 0) const
  {
    ASSERT(elem >= 0 && elem < expr_.ssize());
    return expr_[elem];
  }
  void setRawExpr(String const& expr, int elem = 0)
  {
    ensureVectorSize(expr_, elem + 1);
    expr_[elem] = expr;
    isExpr_[elem] = true;
  }
  void setReal(real value, int elem = 0)
  {
    ensureVectorSize(expr_, elem + 1);
    ensureVectorSize(evaluatedStringValues_, elem + 1);
    isExpr_[elem] = false;
    expr_[elem] = std::to_string(value);
    evaluatedIntValues_[elem] = sint(value);
    evaluatedRealValues_[elem] = value;
    evaluatedStringValues_[elem] = expr_[elem];
    isValid_[elem] = true;

    ++evaluatedVersion_;
  }
  void setInt(sint value, int elem = 0)
  {
    ensureVectorSize(expr_, elem + 1);
    ensureVectorSize(evaluatedStringValues_, elem + 1);
    isExpr_[elem] = false;
    expr_[elem] = std::to_string(value);
    evaluatedIntValues_[elem] = value;
    evaluatedRealValues_[elem] = real(value);
    evaluatedStringValues_[elem] = expr_[elem];
    isValid_[elem] = true;

    ++evaluatedVersion_;
  }
  void setString(String value, int elem = 0)
  {
    ensureVectorSize(expr_, elem + 1);
    ensureVectorSize(evaluatedStringValues_, elem + 1);
    isExpr_[elem] = false;
    expr_[elem] = value;
    evaluatedIntValues_[elem] = std::atoll(value.c_str());
    evaluatedRealValues_[elem] = std::atof(value.c_str());
    evaluatedStringValues_[elem] = expr_[elem];
    isValid_[elem] = true;

    ++evaluatedVersion_;
  }
  void setStringList(Vector<String> slist)
  {
    expr_ = slist;
    evaluatedStringValues_ = std::move(slist);
    std::fill(isExpr_.begin(), isExpr_.end(), false);
    std::fill(isValid_.begin(), isValid_.end(), true);
    for (int i=0, n=std::min(int(slist.size()), MAX_ARG_TUPLE_SIZE); i<n; ++i) {
      evaluatedIntValues_[i] = std::atoll(evaluatedStringValues_[i].c_str());
      evaluatedRealValues_[i] = std::atof(evaluatedStringValues_[i].c_str());
    }
    mutDesc().tupleSize = int(slist.size());
    ++evaluatedVersion_;
  }
  void setBool(bool value)
  {
    isExpr_[0] = false;
    evaluatedIntValues_[0] = value;
    evaluatedRealValues_[0] = value;
    evaluatedStringValues_[0] = value ? "true" : "false";
    expr_[0] = evaluatedStringValues_[0];
    isValid_[0] = true;

    ++evaluatedVersion_;
  }
  void setMenu(int value)
  {
    isExpr_[0] = false;
    evaluatedIntValues_[0] = value;
    evaluatedRealValues_[0] = value;
    evaluatedStringValues_[0] = value >= 0 && value < desc().menu.ssize() ? desc().menu[value] : "";
    expr_[0] = evaluatedStringValues_[0];
    isValid_[0] = true;

    ++evaluatedVersion_;
  }
  void setMenu(String value)
  {
    isExpr_[0] = false;
    auto menuitr = std::find(desc().menu.begin(), desc().menu.end(), value);
    evaluatedIntValues_[0] = menuitr == desc().menu.end() ? -1 : menuitr-desc().menu.begin();
    evaluatedRealValues_[0] = real(evaluatedIntValues_[0]);
    evaluatedStringValues_[0] = value;
    expr_[0] = value;
    isValid_[0] = true;

    ++evaluatedVersion_;
  }

  sint version() const { return evaluatedVersion_; }

  /// evaluation of expressions in arguments, automatically called by runtime
  CORE_API void eval(OpContext* context);

public:
  inline ArgValue(ArgDesc const* desc, OpContext* context);
  inline ArgValue(ArgValue const& av);
  inline ArgValue& operator=(ArgValue const& rhs);

  bool save(Json& self) const;
  bool load(Json const& self);
};

inline ArgValue::ArgValue(ArgDesc const* desc, OpContext* context)
    : desc_(desc)
    , ownDesc_(nullptr)
    , evaluatedVersion_(0)
    , evaluatedStringValues_(1)
    , updateScriptEvaluatedVersion_(-1)
{
  if (desc) {
    std::fill(std::begin(isExpr_), std::end(isExpr_), true);
    std::copy(std::begin(desc->defaultExpression), std::end(desc->defaultExpression), std::back_inserter(expr_));
    evaluatedRealValues_ = {0.f};
    evaluatedIntValues_ = {0};
    eval(context);
  } else {
    expr_.resize(desc ? desc->tupleSize : 1);
  }
}

inline ArgValue::ArgValue(ArgValue const& av)
    : desc_(av.desc_)
    , ownDesc_(av.ownDesc_ ? new ArgDesc : nullptr)
    , evaluatedVersion_(av.evaluatedVersion_)
    , isValid_(av.isValid_)
    , isExpr_(av.isExpr_)
    , expr_(av.expr_)
    , evaluatedRealValues_(av.evaluatedRealValues_)
    , evaluatedIntValues_(av.evaluatedIntValues_)
    , evaluatedStringValues_(av.evaluatedStringValues_)
    , errorMessage_(av.errorMessage_)
    , updateScriptEvaluatedVersion_(av.updateScriptEvaluatedVersion_)
{
  if (ownDesc_)
    *ownDesc_ = *av.ownDesc_;
}

inline ArgValue& ArgValue::operator=(ArgValue const& rhs)
{
  desc_ = rhs.desc_;
  ownDesc_.reset(rhs.ownDesc_ ? new ArgDesc : nullptr);
  evaluatedVersion_      = rhs.evaluatedVersion_;
  isValid_               = rhs.isValid_;
  isExpr_                = rhs.isExpr_;
  expr_                  = rhs.expr_;
  evaluatedRealValues_   = rhs.evaluatedRealValues_;
  evaluatedIntValues_    = rhs.evaluatedIntValues_;
  evaluatedStringValues_ = rhs.evaluatedStringValues_;
  errorMessage_          = rhs.errorMessage_;
  updateScriptEvaluatedVersion_ = rhs.updateScriptEvaluatedVersion_;
  if (ownDesc_)
    *ownDesc_ = *rhs.ownDesc_;
  return *this;
}

inline glm::vec<2, sint> ArgValue::asInt2() const
{
  return glm::vec<2, sint>(evaluatedIntValues_[0], evaluatedIntValues_[1]);
}

inline glm::vec<3, sint> ArgValue::asInt3() const
{
  return glm::vec<3, sint>(evaluatedIntValues_[0], evaluatedIntValues_[1], evaluatedIntValues_[2]);
}

inline glm::vec<4, sint> ArgValue::asInt4() const
{
  return glm::vec<4, sint>(evaluatedIntValues_[0],
                           evaluatedIntValues_[1],
                           evaluatedIntValues_[2],
                           evaluatedIntValues_[3]);
}

inline glm::vec<2, real> ArgValue::asReal2() const
{
  return glm::vec<2, real>(evaluatedRealValues_[0], evaluatedRealValues_[1]);
}

inline glm::vec<3, real> ArgValue::asReal3() const
{
  return glm::vec<3, real>(
      evaluatedRealValues_[0], evaluatedRealValues_[1], evaluatedRealValues_[2]);
}

inline glm::vec<4, real> ArgValue::asReal4() const
{
  return glm::vec<4, real>(evaluatedRealValues_[0],
                           evaluatedRealValues_[1],
                           evaluatedRealValues_[2],
                           evaluatedRealValues_[3]);
}

END_JOYFLOW_NAMESPACE
