#pragma once
#include "../opcontext.h"
#include "../def.h"
#include "../opgraph.h"
#include "../oparg.h"
#include "../opdesc.h"
#include "../datatable.h"
#include "../utility.h"
#include "../profiler.h"

#include "linearmap.h"
#include "runtime.h"

#include <marl/event.h>
#include <marl/scheduler.h>
#include <marl/task.h>

#include <spdlog/spdlog.h>

BEGIN_JOYFLOW_NAMESPACE

namespace detail {

/// Use together with OpDesc:
///   OpDesc is like class definition
///   OpContextImpl is the object instance
class OpContextImpl : public OpContext, public ObjectTracker<OpContextImpl>
{
private:
  std::atomic<bool>         taskScheduled_;
  marl::Event               taskDone_;
  Vector<NodePin>           inputPinInfo_;
  Vector<OpContextImpl*>    inputContexts_;
  Vector<DataCollectionPtr> outputDataCache_;
  Vector<sint>              outputDataVersion_;
  Vector<sint>              inputDataVersionFromLastFetch_;
  Vector<sint>              inputDataVersionFromLastEval_;
  Vector<sint>              argsVersionFromLastEval_;
  Vector<bool>              outputActiveFlag_;
  Vector<bool>              inputDirtyFlag_;
  Vector<bool>              inputUnusedFlag_;
  std::unique_ptr<
    LinearMap<
      String, ArgValue>>    argSnapshot_;
  OpEnvironment const*      environment_ = nullptr;
  OpDesc const*             desc_        = nullptr;
  OpNode*                   node_        = nullptr;
  OpKernelHandle            kernel_      = nullptr;
  bool                      bypassed_    = false;
  bool                      outputActivityDirty_ = false;
  bool                      imFork_      = false; // am i a fork?
  bool                      dirtyFlag_   = false; // anything dirty?
  sint                      evalCount_   = 0;
  mutable std::mutex        errorMutex_;
  OpErrorLevel              errorLevel_  = OpErrorLevel::GOOD;
  String                    errorMessage_;
  bool                      shouldBreak_ = false;
  String                    nodeName_;
  std::unique_ptr<OpStateBlock> stateblock_ = nullptr;

public:
  OpContextImpl(OpNode* node);
  OpContextImpl(OpContextImpl const&);
  ~OpContextImpl();
  OVERRIDE_NEW_DELETE;

  // implementation of OpContext's public interface
  OpDesc const*   desc() const override { return desc_; }
  OpNode*         node() const override { return node_; }
  sint            getNumInputs() const override;
  void            requireInput(sint pin) override;
  DataCollection* fetchInputData(sint pin) override;
  bool            hasInput(sint pin) const override;
  bool            inputDirty(sint pin) const override;
  void            resetInput(sint pin) override;
  bool            argDirty(StringView const& name) const override;
  bool            isDirty() const override { return dirtyFlag_; }
  bool            hasOutputCache(sint pin) const override;
  bool            outputIsActive(sint pin) const override;
  sint            outputVersion(sint pin) const override;
  DataCollection* getOutputCache(sint pin) const override;
  DataCollection* getOrCalculateOutputData(sint pin) override;
  DataCollection* copyInputToOutput(sint pinout, sint pinin) override;
  DataCollection* reallocOutput(sint pin) override;
  void            setOutputData(sint pin, DataCollectionPtr dc) override;
  void            increaseOutputVersion(sint pin) override;
  void            setState(OpStateBlock* state) override { stateblock_.reset(state); }
  OpStateBlock*   getState() const override { return stateblock_.get(); }
  void            markInputDirty(sint pin, bool dirty) override;
  void            markDirty(bool dirty) override { dirtyFlag_ = dirty; }
  void            setOutputActive(sint pin, bool active) override;
  bool            outputActivityDirty() const override;
  void            evalArgument(StringView const& name) override;
  void            evalArguments() override;
  sint            evalCount() const override { return evalCount_; }
  void            evaluate();

  bool setScheduled(bool sch) override
  {
    if (sch)
      taskDone_.signal();
    else
      taskDone_.clear();
    return taskScheduled_.exchange(sch);
  }
  void schedule() override;
  void wait() override;
  void resolveDependency(bool recursive) override;
  OpContext* fork(OpEnvironment const* env) override;
  OpKernelHandle getKernel() const override { return kernel_; }

  OpEnvironment const* env() const override { return environment_; }
  void setEnv(OpEnvironment const* env) override { environment_ = env; }

  ArgValue const& arg(StringView const& name) const override;
  void            reportError(StringView const& message, OpErrorLevel level, bool breakNow) override;

  bool         hasBreakingError() const override { return shouldBreak_; }
  OpErrorLevel lastError() const override { return errorLevel_; }
  String       errorMessage() const override { return errorMessage_; }

  void bindKernel() override { kernel_->bind(*this); }
  void beforeFrameEval() override;
  void beforeEval() override;
  void afterEval() override;
  void afterFrameEval() override;
};

} // namespace detail

END_JOYFLOW_NAMESPACE

