#pragma once

#include "def.h"
#include "opkernel.h"
#include "stringview.h"

BEGIN_JOYFLOW_NAMESPACE

enum class ExecutionPolicy
{
  Sequential,
  Parallel,
};

enum class CachingPolicy
{
  Caching,
  NonCaching
};

/// Global States
/// - but not so 'global' actually, nodes can override its environment if needed,
///   overrided environment will affect its upstream nodes
struct OpEnvironment
{
  real            time = 0.0;
  sint            frame = 0;
  ExecutionPolicy executionPolicy = ExecutionPolicy::Parallel;
  CachingPolicy   cachingPolicy = CachingPolicy::Caching;
};

enum class OpErrorLevel : uint8_t
{
  GOOD = 0,
  WARNING,
  ERROR,
  FATAL
};

/// State block hold by OpContext
/// allocated by OpKernel when needed
/// while OpKernel itself should be stateless
class OpStateBlock
{
public:
  virtual ~OpStateBlock() {}
};

/// Node Execution Context
class OpContext
{
public:
  virtual ~OpContext() {}

  // IO:
  /// number of input data
  virtual sint getNumInputs() const = 0;

  /// require certain input data
  /// fire up `eval` task if needed
  virtual void requireInput(sint pin) = 0;

  /// wait for input task to finish, and returns its data
  virtual DataCollection* fetchInputData(sint pin) = 0;

  /// check if input pin is connected
  virtual bool hasInput(sint pin) const = 0;

  /// query if input data from certain `pin` has changed since last `reallocOutput()`
  /// for `pin=-1`, any of the inputs is dirty gives true
  virtual bool inputDirty(sint pin = -1) const = 0;

  /// query if argument `name` has changed since last `reallocOutput()`
  /// for `name=""`, any of the arguments is dirty gives true
  virtual bool argDirty(StringView const& name = "") const = 0;

  /// reset input after re-route it elsewhere
  virtual void resetInput(sint pin) = 0;

  /// output activity has changed
  virtual bool outputActivityDirty() const = 0;

  /// anything dirty
  virtual bool isDirty() const = 0;

  /// set scheduled flag, returns old scheduled flag
  virtual bool setScheduled(bool sch) = 0;

  /// schedule this node for background evaluation
  virtual void schedule() = 0;

  /// wait for scheduled work to finish
  virtual void wait() = 0;

  /// get up-to-date output data
  virtual DataCollection* getOrCalculateOutputData(sint pin) = 0;

  /// Set/Get state block
  virtual void          setState(OpStateBlock* state) = 0;
  virtual OpStateBlock* getState() const = 0;

  /// inputs / outputs are accessed as context(s)
  /// arg values are copied
  virtual void resolveDependency(bool recursive) = 0;

  /// create a new context (along with all args and dependencies) with environment overriding
  virtual OpContext* fork(OpEnvironment const* env) = 0;

  /// retrives kernel handle
  virtual OpKernelHandle getKernel() const = 0;

  /// numebr of evaluations has been done, usefull to determine if certain error has be repeatly happen
  virtual sint evalCount() const = 0;

  /// query if the runtime has cached previous output of this operator
  virtual bool hasOutputCache(sint pin) const = 0;

  /// check if the output pin is active (rooted from active output)
  virtual bool outputIsActive(sint pin) const = 0;

  /// data version for specified output pin
  virtual sint outputVersion(sint pin) const = 0;

  /// alloc output table and bumps up its data version
  virtual DataCollection* reallocOutput(sint pin) = 0;

  /// use this DataCollection as output
  virtual void setOutputData(sint pin, DataCollectionPtr dc) = 0;

  /// manually mark output as updated
  virtual void increaseOutputVersion(sint pin) = 0;

  /// alloc output table and bumps up its data version
  /// if `pinin` >= 0 then data from input `pinin`
  /// would be copyed to output `pinout`
  virtual DataCollection* copyInputToOutput(sint pinout, sint pinin = 0) = 0;

  /// get last evaluation result
  virtual DataCollection* getOutputCache(sint pin) const = 0;

  // Arg passing:
  virtual ArgValue const& arg(StringView const& name) const = 0;

  // Meta:
  virtual OpDesc const* desc() const = 0;

  // Binding:
  virtual OpNode* node() const = 0;
 
  // TODO:
  virtual OpEnvironment const* env() const = 0;
  virtual void setEnv(OpEnvironment const* env) = 0;

  /// error handling:
  virtual void         reportError(StringView const& message, OpErrorLevel level, bool breakNow) = 0;
  virtual bool         hasBreakingError() const = 0;
  virtual OpErrorLevel lastError() const = 0;
  virtual String       errorMessage() const = 0;

  // these are internal operations, call them only when you know exactly what you are doing
public:
  virtual void markInputDirty(sint pin, bool dirty = true) = 0;
  virtual void markDirty(bool dirty = true) = 0;
  virtual void setOutputActive(sint pin, bool active) = 0;
  virtual void evalArgument(StringView const& name) = 0;
  virtual void evalArguments() = 0;
  virtual void bindKernel() = 0;
  virtual void beforeFrameEval() = 0;
  virtual void beforeEval() = 0;
  virtual void afterEval() = 0;
  virtual void afterFrameEval() = 0;
};

// Global execution context
// TODO: refactor OpNode, OpNode should not hold OpContext and OpGraph should not do execution
//       all exection should start here
class RootContext
{
public:
  virtual void              bind(OpGraph* root) = 0;
  virtual void              unbind() = 0;
  virtual void              addGoal(StringView oppath) = 0; // add goals so that eval() will calculate them
  virtual void              eval() = 0;
  virtual DataCollectionPtr fetch(StringView oppath, sint pin=0) = 0;

  void eval(StringView oppath) { addGoal(oppath); eval(); }
};

OpContext* newOpContext(OpNode* node);

END_JOYFLOW_NAMESPACE
