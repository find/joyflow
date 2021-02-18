#pragma once

#include "def.h"

BEGIN_JOYFLOW_NAMESPACE

class OpContext;
class OpGraph;

class OpKernel
{
public:
  virtual ~OpKernel() {}
  virtual void bind(OpContext& context) {};
  virtual void beforeFrameEval(OpNode* self) {}
  virtual void beforeEval(OpContext& context) const {}
  virtual void eval(OpContext& context) const = 0;
  virtual void afterEval(OpContext& context) const {}
  virtual void afterFrameEval(OpNode* self) {}
};

/// kernel handle adds one level of indirection - making the kernel itself can be reloaded
class OpKernelHandle
{
  OpKernel** ptr_;

public:
  OpKernelHandle(OpKernel* kernel) : ptr_(new OpKernel*) { reset(kernel); }
  OpKernelHandle(OpKernelHandle const& that) : ptr_(new OpKernel*) { reset(*that.ptr_); }
  ~OpKernelHandle() { delete ptr_; }
  OpKernel*       operator->() { return *ptr_; }
  OpKernel*       operator*() { return *ptr_; }
  OpKernel const* operator->() const { return *ptr_; }
  OpKernel const* operator*() const { return *ptr_; }
  void            reset(OpKernel* kernel) { *ptr_ = kernel; }
};


END_JOYFLOW_NAMESPACE
