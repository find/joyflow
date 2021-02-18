#include "opcontext_detail.h"

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

#ifdef __unix__
#include <sys/types.h>
#include <sys/syscall.h>
#include <unistd.h>
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOATOM
#define NOGDI
#define NOGDICAPMASKS
#define NOMETAFILE
#define NOMINMAX
#define NOMSG
#define NOOPENFILE
#define NORASTEROPS
#define NOSCROLL
#define NOSOUND
#define NOSYSMETRICS
#define NOTEXTMETRIC
#define NOWH
#define NOCOMM
#define NOKANJI
#define NOCRYPT
#define NOMCX
#include <Windows.h>
static inline size_t gettid()
{
  return ::GetCurrentThreadId();
}
#endif

BEGIN_JOYFLOW_NAMESPACE

namespace detail {

OpContextImpl::OpContextImpl(OpNode* node) :
  taskScheduled_(false),
  taskDone_(marl::Event::Mode::Manual),
  node_(node),
  kernel_(OpRegistry::instance().createOp(node->desc()->name)),
  desc_(node->desc()),
  inputDataVersionFromLastEval_(node->desc()->numMaxInput, -1),
  inputDataVersionFromLastFetch_(node->desc()->numMaxInput, -1),
  inputDirtyFlag_(node->desc()->numMaxInput, true),
  inputUnusedFlag_(node->desc()->numMaxInput, false),
  outputDataCache_(node->desc()->numOutputs, nullptr),
  outputDataVersion_(node->desc()->numOutputs, 0),
  outputActiveFlag_(node->desc()->numOutputs, false)
{
}

OpContextImpl::OpContextImpl(OpContextImpl const& that):
  taskScheduled_(false),
  taskDone_(marl::Event::Mode::Manual),
  node_(that.node_),
  kernel_(OpRegistry::instance().createOp(that.desc()->name)),
  desc_(that.desc_),
  inputPinInfo_(that.inputPinInfo_),
  inputContexts_(that.inputContexts_.size()),
  outputDataCache_(),
  outputDataVersion_(),
  inputDataVersionFromLastFetch_(),
  inputDataVersionFromLastEval_(),
  argsVersionFromLastEval_(that.argsVersionFromLastEval_),
  outputActiveFlag_(that.outputActiveFlag_),
  inputDirtyFlag_(that.inputDirtyFlag_.size()),
  inputUnusedFlag_(that.inputUnusedFlag_.size()),
  argSnapshot_(nullptr),
  environment_(nullptr),
  outputActivityDirty_(that.outputActivityDirty_),
  imFork_(true),
  bypassed_(that.bypassed_),
  evalCount_(that.evalCount_),
  errorMutex_(),
  errorLevel_(OpErrorLevel::GOOD),
  errorMessage_(),
  shouldBreak_(false),
  nodeName_(that.nodeName_)
{
  if (node_->argCount() > 0) {
    argSnapshot_.reset(new LinearMap<String, ArgValue>());
    for (sint argi = 0, argc = node_->argCount(); argi < argc; ++argi) {
      auto name = node_->argName(argi);
      argSnapshot_->insert(name, node_->arg(name));
    }
  }
  for (auto const* upstream: that.inputContexts_) {
    if (upstream)
      inputContexts_.push_back(new OpContextImpl(*upstream));
    else
      inputContexts_.push_back(nullptr);
  }
}
  

OpContextImpl::~OpContextImpl()
{
  OpRegistry::instance().destroyOp(kernel_);
  if (imFork_) {
    for (auto* upstream : inputContexts_)
      if (upstream)
        delete upstream;
  }
}

sint OpContextImpl::getNumInputs() const
{
  return static_cast<sint>(inputContexts_.size());
}

void OpContextImpl::evaluate()
{
  ASSERT(nodeName_[0]!='<');
  if (*kernel_ && !bypassed_) {
    // if something was wrong, and nothing was done to solve that, skip evaluation
    if (shouldBreak_ && !isDirty()) {
      spdlog::debug("skipping previously failed node {}", nodeName_);
      return;
    }
    PROFILER_SCOPE("OpNode::evaluate", 0x815476);
    PROFILER_TEXT(nodeName_.c_str(), nodeName_.length());
    spdlog::trace("{}: evaluating at thread {}...", nodeName_, gettid());
    beforeEval();
    try {
      kernel_->eval(*this);
    } catch (CheckFailure const& f) {
      spdlog::error("check failure: {}", f.what());
      reportError(f.what(), OpErrorLevel::ERROR, false); // pass on to the calling thread
    } catch (ExecutionError const& f) {
      spdlog::error("execution error: {}", f.what());
      reportError(f.what(), OpErrorLevel::ERROR, false); // pass on to the calling thread
    } catch (std::exception const& e) {
      spdlog::error("std::exception: {}", e.what());
      reportError(e.what(), OpErrorLevel::ERROR, false); // pass on to the calling thread
    }
    afterEval();
    spdlog::trace("{}: done.", nodeName_);
  } else if (desc()->numOutputs>0) {
    beforeEval();
    if (hasInput(0))
      copyInputToOutput(0, 0);
    else
      reallocOutput(0);
    for (sint i=1, n=desc()->numOutputs; i<n; ++i) {
      if (outputIsActive(i))
        reallocOutput(i);
    }
    afterEval();
    spdlog::warn("{}: bypassed", nodeName_);
  }
}

void OpContextImpl::schedule()
{
  // don't schedule lightweight tasks
  if ((desc()->flags&OpFlag::LIGHTWEIGHT)!=OpFlag::LIGHTWEIGHT &&
      !taskScheduled_.exchange(true)) {
    spdlog::debug("schedulered {} ...", nodeName_);
    PROFILER_SCOPE("Scheduling", 0x4C8DAE);
    taskDone_.clear();
    TaskContext::instance().scheduler.enqueue(marl::Task([=]{
      PROFILER_SCOPE("marl Task", 0xC0EBD7);
      try {
        evaluate();
      } catch (std::exception const& e) {
        std::fill(outputDataCache_.begin(), outputDataCache_.end(), nullptr);
        afterEval();
        reportError(e.what(), OpErrorLevel::ERROR, false); // pass on to the calling thread
      }
      taskDone_.signal();
    }));
  }
}

void OpContextImpl::wait()
{
  bool evalWasCalled = false;
  if (!taskScheduled_.exchange(true)) {
    // evaluate inline
    evalWasCalled = true;
    try {
      evaluate();
      taskDone_.signal();
    } catch (std::exception const& e) {
      std::fill(outputDataCache_.begin(), outputDataCache_.end(), nullptr);
      afterEval();
      taskDone_.signal();
      reportError(e.what(), OpErrorLevel::ERROR, true);
    }
  } else if(!taskDone_.isSignalled()) {
    evalWasCalled = true;
    PROFILER_SCOPE("Wait", 0xFF2121);
    std::string profiletxt = fmt::format("Wait for {}", nodeName_);
    PROFILER_TEXT(profiletxt.c_str(), profiletxt.length());
    spdlog::trace("waiting for {} ...", nodeName_);
    taskDone_.wait();
    spdlog::trace("waiting for {} ... done.", nodeName_);
  }
  if (evalWasCalled) {
    // TODO: move this to elsewhere
    std::lock_guard errLock(errorMutex_);
    if (shouldBreak_) { // bad thing happend to my job
      std::fill(outputDataCache_.begin(), outputDataCache_.end(), nullptr);
      afterEval();
      throw ExecutionError(errorMessage_);
    }
  }
}

void OpContextImpl::resolveDependency(bool recursive)
{
  // TODO: recursive
  inputContexts_.resize(node_->upstreams().size());
  inputPinInfo_ = node_->upstreams();
  if (auto bypassnow = node_->isBypassed(); bypassnow != bypassed_) {
    bypassed_ = bypassnow;
    dirtyFlag_ = true;
  }
  std::fill(inputContexts_.begin(), inputContexts_.end(), nullptr);
  auto* graph = node_->parent();
  for (size_t i=0, n=node_->upstreams().size(); i<n; ++i) {
    auto pin = node_->upstreams()[i];
    if (pin.isValid()) {
      auto ictx = static_cast<OpContextImpl*>(graph->node(pin.name)->context());
      ASSERT(ictx);
      inputContexts_[i] = ictx;
    }
  }
  nodeName_ = node_->name();
  // TODO: also copy arguments
}

OpContext* OpContextImpl::fork(OpEnvironment const* env)
{
  auto* ctx = new OpContextImpl(*this);
  ctx->setEnv(env);
  return ctx;
}

void OpContextImpl::requireInput(sint pin)
{
  if (!hasInput(pin))
    return;
  auto *ictx = inputContexts_[pin];
  DEBUG_ASSERT(ictx);
  if (!ictx->isDirty())
    return;
  inputUnusedFlag_[pin] = false;
  ictx->schedule();
}

DataCollection* OpContextImpl::fetchInputData(sint pin)
{
  ALWAYS_ASSERT(hasInput(pin));
  auto* ictx = inputContexts_[pin];
  DEBUG_ASSERT(ictx);
  try {
    inputUnusedFlag_[pin] = false;
    auto* dc = ictx->getOrCalculateOutputData(inputPinInfo_[pin].pin);
    if (!dc || ictx->shouldBreak_) {
      throw ExecutionError(ictx->errorMessage_);
    }
    inputDataVersionFromLastFetch_[pin] = ictx->outputVersion(inputPinInfo_[pin].pin);
    return dc;
  } catch(std::exception const& e) {
    reportError(fmt::format("upstream {} failed because:\n{}", ictx->nodeName_, e.what()), ictx->errorLevel_, true);
  }
  return nullptr;
}

bool OpContextImpl::hasInput(sint pin) const
{
  return pin >= 0 && pin<inputContexts_.ssize() && inputContexts_[pin];
}

bool OpContextImpl::inputDirty(sint pin) const
{
  ALWAYS_ASSERT(pin<0 || hasInput(pin));
  if (getNumInputs() == 0)
    return false;
  if (pin < 0) { // any of the inputs is dirty
    // not all input data has been pulled
    if (inputDataVersionFromLastEval_.ssize() < getNumInputs())
      return true;
    for (sint i = 0, n = getNumInputs(); i < n; ++i) {
      auto* ictx = inputContexts_[i];
      if (!ictx) {
        if (inputDataVersionFromLastEval_[i] > 0) // previously has valid input data, now gone
          return true;
        else
          continue;
      }
      if (inputDataVersionFromLastEval_[i] < ictx->outputVersion(inputPinInfo_[i].pin)) {
        // loop pin are ignored, check it yourself, otherwise all loops would be infinite
        using FlagEnumInt = std::underlying_type_t<OpFlag>;
        bool const isLoopPin =
          (!!(desc()->flags & OpFlag::ALLOW_LOOP) &&
            i<=FlagEnumInt(OpFlag::LOOPPIN_MAXCOUNT) &&
            !!(desc()->flags & OpFlag(1<<(FlagEnumInt(OpFlag::LOOPPIN_BITSHIFT)+i))));
        if (!isLoopPin)
          return true;
      }
      if (inputUnusedFlag_.ssize() <= i || !inputUnusedFlag_[i]) {
        if (ictx->isDirty())
          return true;
        if (inputDirtyFlag_.ssize() > i && inputDirtyFlag_[i])
          return true;
      }
    }
    return false;
  } else { // specific input is dirty
    if (!hasInput(pin))
      return false;
    auto *ictx = inputContexts_[pin];
    if (!ictx)
      return false;
    if (inputDataVersionFromLastEval_.ssize() <= pin)
      return true;
    if (inputUnusedFlag_.ssize() <= pin || !inputUnusedFlag_[pin]) {
      if (ictx->isDirty())
        return true;
      if (inputDirtyFlag_.ssize() > pin && inputDirtyFlag_[pin])
        return true;
    }
    return inputDataVersionFromLastEval_[pin] < ictx->outputVersion(inputPinInfo_[pin].pin);
  }
}

void OpContextImpl::resetInput(sint pin)
{
  if (pin >= 0 && pin < inputDataVersionFromLastEval_.ssize()) {
    ensureVectorSize(inputDataVersionFromLastFetch_, pin + 1);
    ensureVectorSize(inputDirtyFlag_, pin + 1);
    ensureVectorSize(inputUnusedFlag_, pin + 1);
    inputDataVersionFromLastEval_[pin] = -1;
    inputDataVersionFromLastFetch_[pin] = -1;
    inputDirtyFlag_[pin] = true;
    inputUnusedFlag_[pin] = false;
  }
}

bool OpContextImpl::argDirty(StringView const& name) const
{
  if (name.empty()) { // any of the args is dirty
    if (node_->argCount() != argsVersionFromLastEval_.size())
      return true;
    for (size_t i = 0, n = node_->argCount(); i < n; ++i) {
      if (node_->argVersion(i) > argsVersionFromLastEval_[i])
        return true;
    }
    return false;
  } else { // specific arg is dirty
    sint idx = node_->argIndex(name);
    if (idx < 0 || size_t(idx) >= argsVersionFromLastEval_.size())
      return false;
    return node_->argVersion(idx) > argsVersionFromLastEval_[idx];
  }
}

bool OpContextImpl::hasOutputCache(sint pin) const
{
  return pin >= 0 && pin < outputDataCache_.ssize() && outputDataCache_[pin];
}

DataCollection* OpContextImpl::getOutputCache(sint pin) const
{
  return pin >= 0 && pin < outputDataCache_.ssize() ? outputDataCache_[pin].get() : nullptr;
}

DataCollection* OpContextImpl::getOrCalculateOutputData(sint pin)
{
  if (!hasOutputCache(pin) || isDirty()) { // need re-evaluation
    schedule();
    wait();
  }
  return getOutputCache(pin);
}

bool OpContextImpl::outputIsActive(sint pin) const
{
  if (pin < 0 || pin >= desc_->numOutputs || pin >= outputActiveFlag_.ssize())
    return false;
  return outputActiveFlag_[pin];
}

sint OpContextImpl::outputVersion(sint pin) const
{
  if (pin >= 0 && pin < outputDataVersion_.ssize())
    return outputDataVersion_[pin];
  else
    return 0;
}

DataCollection* OpContextImpl::reallocOutput(sint pin)
{
  ASSERT(pin >= 0 && pin < desc_->numOutputs);
  outputDataCache_[pin] = newDataCollection();
  if (inputDataVersionFromLastEval_.empty())
    ++outputDataVersion_[pin];
  else {
    sint maxInputVersion = -1;
    for (auto iv : inputDataVersionFromLastFetch_)
      maxInputVersion = std::max(maxInputVersion, iv);
    outputDataVersion_[pin] = maxInputVersion + 1;
  }
  return outputDataCache_[pin].get();
}

DataCollection* OpContextImpl::copyInputToOutput(sint pin, sint copyFromInput)
{
  ASSERT(pin >= 0 && pin < desc_->numOutputs);
  ASSERT(copyFromInput < desc_->numMaxInput);
  if (copyFromInput >= 0) {
    // TODO: if this node is the only successor of its upstream,
    //       it's safe to directly reuse upstream data
    if (hasInput(copyFromInput))
      outputDataCache_[pin] = fetchInputData(copyFromInput)->share();
    else
      outputDataCache_[pin] = newDataCollection();
  } else {
    outputDataCache_[pin] = newDataCollection();
  }
  if (inputDataVersionFromLastEval_.empty())
    ++outputDataVersion_[pin];
  else {
    sint maxInputVersion = -1;
    for (auto iv : inputDataVersionFromLastFetch_)
      maxInputVersion = std::max(maxInputVersion, iv);
    outputDataVersion_[pin] = maxInputVersion + 1;
  }
  return outputDataCache_[pin].get();
}

void OpContextImpl::setOutputData(sint pin, DataCollectionPtr dc)
{
  outputDataCache_[pin] = dc;
  if (inputDataVersionFromLastEval_.empty())
    ++outputDataVersion_[pin];
  else {
    sint maxInputVersion = -1;
    for (auto iv : inputDataVersionFromLastFetch_)
      maxInputVersion = std::max(maxInputVersion, iv);
    outputDataVersion_[pin] = maxInputVersion + 1;
  }
}

void OpContextImpl::increaseOutputVersion(sint pin)
{
  DEBUG_ASSERT(pin >= 0 && pin < desc()->numOutputs);
  ensureVectorSize(outputDataVersion_, pin + 1);
  ++outputDataVersion_[pin];
}

ArgValue const& OpContextImpl::arg(StringView const& name) const
{
  if (argSnapshot_) {
    auto parg = argSnapshot_->find(name);
    RUNTIME_CHECK(parg, "arg \"{}\" does not exist", name);
    return *parg;
  } else {
    return node_->arg(name);
  }
}

void OpContextImpl::reportError(StringView const& message,
                                OpErrorLevel      level,
                                bool              breakNow)
{
  std::lock_guard errLock(errorMutex_);
  errorLevel_ = std::max(level, errorLevel_);
  errorMessage_ = message;
  shouldBreak_ = shouldBreak_ || breakNow || level >= OpErrorLevel::ERROR;
  spdlog::error(message);
  if (breakNow)
    throw ExecutionError(errorMessage_);
}

void OpContextImpl::beforeFrameEval()
{
  resolveDependency(false);
  //ensureVectorSize(inputUnusedFlag_, inputContexts_.size());
  //std::fill(inputUnusedFlag_.begin(), inputUnusedFlag_.end(), true);
  try {
    kernel_->beforeFrameEval(node_);
  } catch(std::exception const& e) {
    reportError(e.what(), OpErrorLevel::ERROR, true);
  }
}

void OpContextImpl::beforeEval()
{
  ++evalCount_;
  {
    std::lock_guard errLock(errorMutex_);
    errorLevel_ = OpErrorLevel::GOOD;
    errorMessage_ = "";
    shouldBreak_ = false;
  }
  kernel_->beforeEval(*this);
  sint numValidInputs = 0;

  // always mark as fetched ..
  // for situations where links between nodes only marks dependency, while data is not used by downstream
  inputDataVersionFromLastFetch_.resize(inputContexts_.size());
  for (size_t i=0;i<inputContexts_.size(); ++i) {
    if (hasInput(i)) {
      inputDataVersionFromLastFetch_[i] = std::max(inputDataVersionFromLastFetch_[i], inputContexts_[i]->outputVersion(inputPinInfo_[i].pin));
      ++numValidInputs;
    } else {
      inputDataVersionFromLastFetch_[i] = -1;
    }
  }
  ensureVectorSize(inputUnusedFlag_, inputContexts_.size());
  std::fill(inputUnusedFlag_.begin(), inputUnusedFlag_.end(), true);

  if (numValidInputs < desc()->numRequiredInput)
    reportError("Input missing", OpErrorLevel::FATAL, true);
}

void OpContextImpl::afterEval()
{
  inputDataVersionFromLastEval_ = inputDataVersionFromLastFetch_;
  argsVersionFromLastEval_.resize(node_->argCount());
  for (size_t i = 0, n = node_->argCount(); i < n; ++i)
    argsVersionFromLastEval_[i] = node_->argVersion(i);
  std::fill(inputDirtyFlag_.begin(), inputDirtyFlag_.end(), false);
  outputActivityDirty_ = false;
  dirtyFlag_ = false;
  kernel_->beforeEval(*this);
  taskDone_.clear();
  taskScheduled_.store(false);
}

void OpContextImpl::afterFrameEval()
{
  kernel_->afterFrameEval(node_);
  inputContexts_.clear();
  inputPinInfo_.clear();
  nodeName_ = "<detached node>";
}

void OpContextImpl::markInputDirty(sint pin, bool dirty)
{
  ASSERT(pin >= 0 && pin < desc_->numMaxInput);
  inputDirtyFlag_[pin] = dirty;
}

void OpContextImpl::setOutputActive(sint pin, bool active)
{
  ASSERT(pin >= 0 && pin < desc_->numOutputs);
  if (outputActiveFlag_[pin] != active)
    outputActivityDirty_ = true;
  outputActiveFlag_[pin] = active;
}

bool OpContextImpl::outputActivityDirty() const
{
  return outputActivityDirty_;
}

void OpContextImpl::evalArgument(StringView const& name)
{
  ASSERT(node_);
  ALWAYS_ASSERT(!imFork_);
  node_->evalArgument(name);
}

void OpContextImpl::evalArguments()
{
  ASSERT(node_);
  ALWAYS_ASSERT(!imFork_);
  node_->evalAllArguments();
}

static struct OpContextInspectorRegister {
  OpContextInspectorRegister() {
    ObjectInspector insp = {
      [](void const* ctx)->String {
        return static_cast<OpContextImpl const*>(ctx)->node()->name();
      }
    };
    Stats::setInspector<OpContextImpl>(insp);
  }
} _reg = {};

} // namespace detail

OpContext* newOpContext(OpNode* node)
{
  return new detail::OpContextImpl(node);
}

void deleteOpContext(OpContext* ctx)
{
  delete static_cast<detail::OpContextImpl*>(ctx);
}


END_JOYFLOW_NAMESPACE
