#pragma once

#include "../datatable.h"
#include "../error.h"
#include "../opcontext.h"
#include "../opdesc.h"
#include "../oparg.h"
#include "../opgraph.h"
#include "../opkernel.h"

#include "../vector.h"
#include "../stats.h"
#include "../utility.h"
#include "../profiler.h"

#include "linearmap.h"
#include "runtime.h"
#include "serialize.h"

#include <nlohmann/json.hpp>

BEGIN_JOYFLOW_NAMESPACE

class OpGraphImpl;

// OpNode {{{
class OpNodeImpl
    : public OpNode
    , public ObjectTracker<OpNodeImpl>
{
protected:
  OpDesc const*              desc_ = nullptr;
  String                     name_;
  // the graph this ndoe belongs to. there is a root graph for top-level nodes
  OpGraph*                   parent_  = nullptr;
  std::unique_ptr<OpContext> context_ = nullptr;
  size_t                     id_      = 0;

  bool                       bypass_ = false;

  OpEnvironment const* environment_ = nullptr;
  std::unique_ptr<OpEnvironment> ownEnvironment_ = nullptr;

  // my input pin -> referenced input data (another node's output pin)
  Vector<NodePin> upstreams_;
  // my output pin -> referencing nodes (that node's input pin)
  Vector<HashSet<NodePin, NodePin::Hasher>> downstreams_;
  // my arguments
  LinearMap<String, ArgValue> argValues_;

  friend class OpContext;

public:
  OpNodeImpl(String const& name, OpGraphImpl* parent, OpDesc const* desc);
  virtual ~OpNodeImpl();
  OVERRIDE_NEW_DELETE;

  DataCollectionPtr getOutput(sint pin = 0) override;

  void              setName(String newname) { name_ = std::move(newname); }

public:
  OpDesc const*   desc() const override;
  String          optype() const override { return desc_->name; }
  OpGraph*        parent() const override { return parent_; }
  virtual OpNode* node(StringView const& name) const override { return nullptr; } // subnode
  String          name() const override { return name_; }
  size_t          id() const override { return id_; }

  void       setContext(OpContext* context) override { context_.reset(context); }
  void       newContext() override { context_.reset(newOpContext(this)); }
  OpContext* context() const override { return context_.get(); }

  bool       isBypassed() const override { return bypass_; }
  void       setBypassed(bool bypass) override { bypass_ = bypass; }

  void                 setEnv(OpEnvironment const* env) override { environment_ = env; }
  void                 overrideEnv(OpEnvironment env) override { ownEnvironment_.reset(new OpEnvironment(std::move(env))); }
  OpEnvironment const* env() const override { return ownEnvironment_ ? ownEnvironment_.get() : environment_; }

  size_t                argCount() const override { return argValues_.size(); }
  sint                  argVersion(size_t idx) const override { return argValues_[idx].version(); }
  sint                  argIndex(StringView const& name) const override { return argValues_.indexof(name); }
  String                argName(sint idx) const override { return argValues_.key(idx); }
  void                  evalArgument(StringView const& name) override;
  void                  evalAllArguments() override;
  Vector<String> const& argNames() const { return argValues_.keys(); }

  ArgValue const& arg(sint idx) const override;
  ArgValue const& arg(StringView const& name) const override { return arg(argIndex(name)); }
  ArgValue&       mutArg(StringView const& name) override;

  size_t countMemory() const {
    size_t nbytes = sizeof(*this);
    nbytes += sizeof(NodePin) * upstreams_.capacity();
    for (auto const& ds: downstreams_)
      nbytes += sizeof(NodePin) * ds.size();
    nbytes += (sizeof(String)+sizeof(ArgValue)+sizeof(size_t))*argValues_.keys().capacity();
    return nbytes;
  }

public:
  Vector<NodePin> const&        upstreams() const override { return upstreams_; }
  decltype(downstreams_) const& downstreams() const override { return downstreams_; }
  void                          setUpstream(sint inputPin, NodePin const& outputPin) override;
  void                          addToDownstream(sint outputPin, NodePin const& inputPin) override;
  void removeFromDownstream(sint outputPin, NodePin const& inputPin) override;

  bool save(Json& doc) const override;
  bool load(Json const& doc) override;
};
// OpNode }}}

// OpGraph {{{
class OpGraphImpl
    : public OpNodeImpl
    , public OpGraph
    , public ObjectTracker<OpGraphImpl>
{
  LinearMap<String, OpNode*>  children_;
  // context(s) are stored together to make it easy to take 'snapshots' and/or do statistics
  Vector<size_t>              outputNodes_;
  OpDesc*                     ownDesc_ = nullptr;

  OpGraphImpl(OpGraphImpl const&) = delete;

protected:
  /// updates dependency & dirty flag
  /// if `nodeToResolve` exists, then that node will be counted as the only output of this graph
  void prepareEvaluation(String const& nodeToResolve = "");
  void cleanupEvaluation();

public:
  OpGraphImpl(String const& name, OpGraph* parent);
  virtual ~OpGraphImpl();
  OVERRIDE_NEW_DELETE;

  // node interface:
  OpDesc const*  desc() const override { return ownDesc_; }
  String         optype() const override { return ownDesc_->name; }
  OpGraph*       parent() const override { return parent_; }
  String         name() const override { return name_; }
  size_t         id() const override { return id_; }

  size_t argCount() const override { return OpNodeImpl::argCount(); }
  sint   argVersion(size_t idx) const override { return OpNodeImpl::argVersion(idx); }
  sint   argIndex(StringView const& name) const override { return OpNodeImpl::argIndex(name); }
  String argName(sint idx) const override { return OpNodeImpl::argName(idx); }
  void   evalArgument(StringView const& name) override { OpNodeImpl::evalArgument(name); }
  void   evalAllArguments() override { OpNodeImpl::evalAllArguments(); }

  ArgValue const& arg(sint idx) const override { return OpNodeImpl::arg(idx); }
  ArgValue const& arg(StringView const& name) const override { return OpNodeImpl::arg(name); }
  ArgValue&       mutArg(StringView const& name) override { return OpNodeImpl::mutArg(name); }

  void       setContext(OpContext* context) override { OpNodeImpl::setContext(context); }
  void       newContext() override { OpNodeImpl::newContext(); }
  OpContext* context() const override { return OpNodeImpl::context(); }

  bool       isBypassed() const override { return OpNodeImpl::isBypassed(); }
  void       setBypassed(bool bypass) override { OpNodeImpl::setBypassed(bypass); }

  void                 setEnv(OpEnvironment const* env) override { OpNodeImpl::setEnv(env); }
  void                 overrideEnv(OpEnvironment env) override { OpNodeImpl::overrideEnv(env); }
  OpEnvironment const* env() const override { return OpNodeImpl::env(); }


  Vector<NodePin> const& upstreams() const override { return OpNodeImpl::upstreams(); }
  Vector<HashSet<NodePin, NodePin::Hasher>> const& downstreams() const override
  {
    return OpNodeImpl::downstreams();
  }
  void setUpstream(sint inputPin, NodePin const& outputPin) override
  {
    OpNodeImpl::setUpstream(inputPin, outputPin);
  }
  void addToDownstream(sint outputPin, NodePin const& inputPin) override
  {
    OpNodeImpl::addToDownstream(outputPin, inputPin);
  }
  void removeFromDownstream(sint outputPin, NodePin const& inputPin) override
  {
    OpNodeImpl::removeFromDownstream(outputPin, inputPin);
  }

  String  addNode(String const& optype, String const& name) override;
  bool    removeNode(String const& name) override;
  bool    renameNode(String const& original, String const& desired, String& accepted) override;
  OpNode* node(StringView const& name) const override
  {
    auto ptr = children_.find(name);
    return ptr ? *ptr : nullptr;
  }
  Vector<String> const& childNames() const override { return children_.keys(); }
  bool link(String const& srcname, sint srcpin, String const& destname, sint destpin) override;
  bool unlink(String const& destname, sint destpin) override;
  bool unlink(String const& srcname, sint srcpin, String const& destname, sint destpin) override;
  bool setOutputNode(sint pin, String const& name, bool output = true) override;

public:
  OpDesc& mutDesc() override { return *ownDesc_; }

  class GraphEval : public OpKernel
  {
  public:
    virtual void eval(OpContext& context) const override;
  };

  DataCollectionPtr getOutput(sint pin) override;

  DataCollectionPtr evalNode(String const& name, sint pin) override;

  bool save(Json& doc) const override;
  bool load(Json const& doc) override;
};
// OpGraph }}}

// Preset Registry {{{
class OpGraphPresetRegistryImpl : public OpGraphPresetRegistry
{
  struct PresetDefinition
  {
    String filepath;
    Json   definition;
    bool   isEmbed  :1;
    bool   isShared :1;
  };
  HashMap<String, PresetDefinition>         definitions_;

public:
  /// register a new preset, named `name`, defined by `def`
  bool add(String const& path, String const& presetName, Json const& def, bool shared) override;

  /// query if such preset is defined
  bool registered(String const& presetName) override;

  /// scan dir and load / reload all definitions there
  // TODO: scandir
  // void scandir(String const& path) override;

  /// create a new instance of preset `name`
  /// the instance is shared and read-only
  /// OpGraphPresetRegistry will keep track of it, so it can
  /// be reloaded
  /// if no preset with such name was registed, then a dummy
  /// graph that does nothing but copy its first input to its
  /// output will be returned
  OpGraph* create(String const& presetName, String const& nodeName) override;

  /// create a new editable folk of preset `name`
  /// if no preset with such name was registed, then an empty
  /// graph will be returned
  OpGraph* createFolk(String const& presetName, String const& nodeName) override;

  /// destroy this preset instance
  void destroy(OpGraph* graph) override;
};
// Preset Registry }}}

END_JOYFLOW_NAMESPACE

