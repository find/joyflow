#pragma once

#include "def.h"
#include "oparg.h"
#include "opkernel.h"
#include "vector.h"

#include <algorithm>

BEGIN_JOYFLOW_NAMESPACE

class OpKernel;

enum class OpFlag : uint32_t
{
  REGULAR = 0,          //< nothing unusual
  LIGHTWEIGHT = 1,      //< light weight op prefers running inline (without scheduling)
  DANGEROUS  = 1 << 1,  //< dangerous op needs confirm before running during development
  ALLOW_LOOP = 1 << 2,  //< allow loop
  LOOP_PIN0  = 1 << 3,  //< pin0 can link to the loop
  LOOP_PIN1  = 1 << 4,  //< pin1 can link to the loop
  LOOP_PIN2  = 1 << 5,  //< pin2 can link to the loop

  LOOPPIN_BITSHIFT = 3, // helper for loop pin checking
  LOOPPIN_MAXCOUNT = 3, // helper for loop pin checking
};
ENABLE_BITWISE_OP_FOR_ENUM_CLASS(OpFlag)

struct OpDesc
{
  String          name             = "";    //< name for referencing
  sint            numRequiredInput = 1;     //< minimal number of inputs needed
  sint            numMaxInput      = 4;     //< maximal number of inputs accepted
  sint            numOutputs       = 1;     //< number of outputs
  Vector<String>  inputPinNames    = {};    //< pin name
  Vector<String>  outputPinNames   = {};    //< pin name
  Vector<ArgDesc> argDescs         = {};    //< arguments in order
  String          icon             = "\xEF\x82\x85";    //< FontAwesome icon /*ICON_FA_COGS*/
  OpFlag          flags            = OpFlag::REGULAR;

  OpKernel* (*createKernel)()      = nullptr; //< instanciation method
  void (*destroyKernel)(OpKernel*) = nullptr; //< clean-up method
};

class OpDescBuilder
{
  OpDesc desc_;

public:
  OpDescBuilder(OpDesc const& initial = {}) : desc_(initial) {}
                 operator OpDesc() const { return desc_; }
  OpDesc const&  get() const { return desc_; }
  OpDescBuilder& name(String const& n)
  {
    desc_.name = n;
    return *this;
  }
  OpDescBuilder& numRequiredInput(sint n)
  {
    desc_.numRequiredInput = n;
    desc_.numMaxInput      = std::max(desc_.numMaxInput, n);
    return *this;
  }
  OpDescBuilder& numMaxInput(sint n)
  {
    desc_.numMaxInput      = n;
    desc_.numRequiredInput = std::min(desc_.numRequiredInput, n);
    return *this;
  }
  OpDescBuilder& numOutputs(sint n)
  {
    desc_.numOutputs = n;
    return *this;
  }
  OpDescBuilder& inputPinNames(Vector<String> const& names)
  {
    desc_.inputPinNames = names;
    return *this;
  }
  OpDescBuilder& inputPinNames(Vector<String>&& names)
  {
    desc_.inputPinNames = names;
    return *this;
  }
  OpDescBuilder& outputPinNames(Vector<String> const& names)
  {
    desc_.outputPinNames = names;
    return *this;
  }
  OpDescBuilder& outputPinNames(Vector<String>&& names)
  {
    desc_.outputPinNames = names;
    return *this;
  }
  OpDescBuilder& argDescs(Vector<ArgDesc> const& args)
  {
    desc_.argDescs = args;
    return *this;
  }
  OpDescBuilder& argDescs(Vector<ArgDesc>&& args)
  {
    desc_.argDescs = args;
    return *this;
  }
  OpDescBuilder& icon(String icon)
  {
    desc_.icon = std::move(icon);
    return *this;
  }
  OpDescBuilder& flags(OpFlag flags)
  {
    desc_.flags = flags;
    return *this;
  }
  OpDescBuilder& createKernel(OpKernel* (*createfn)())
  {
    desc_.createKernel = createfn;
    return *this;
  }
  OpDescBuilder& destroyKernel(void (*destroyfn)(OpKernel*))
  {
    desc_.destroyKernel = destroyfn;
    return *this;
  }
};

template<class T>
OpDescBuilder makeOpDesc(String const& name)
{
  OpDesc desc;
  desc.name          = name;
  desc.createKernel  = []() -> OpKernel* { return new T; };
  desc.destroyKernel = [](OpKernel* op) { delete op; };
  return OpDescBuilder(desc);
}

class CORE_API OpRegistry
{
public:
  static OpRegistry& instance();

  virtual OpDesc const*  get(String const& name) const                          = 0;
  virtual Vector<String> list() const                                           = 0;
  virtual bool           add(OpDesc const& desc, bool overwriteExisting = true) = 0;
  virtual void           remove(String const& name)                             = 0;
  virtual void           remove(OpDesc const& desc)                             = 0;
  virtual bool           replace(String const& name, OpDesc const& newDesc)     = 0;
  virtual OpKernelHandle createOp(String const& name)                           = 0;
  virtual void           destroyOp(OpKernelHandle op)                           = 0;
};

END_JOYFLOW_NAMESPACE
