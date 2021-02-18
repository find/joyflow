#pragma once

#include "def.h"
#include "stringview.h"
#include "intrusiveptr.h"
#include "opkernel.h"
#include "vector.h"

BEGIN_JOYFLOW_NAMESPACE

struct NodePin
{
  String name = "";
  sint   pin  = -1;

  struct Hasher
  {
    size_t operator()(NodePin const& np) const
    {
      return std::hash<String>()(np.name) ^ std::hash<sint>()(np.pin);
    }
  };
  bool operator==(NodePin const& rhs) const { return name == rhs.name && pin == rhs.pin; }
  bool isValid() const { return !name.empty() && pin >= 0; }
};

struct NodeLink
{
  NodePin source;
  NodePin destiny;
};

struct OpEnvironment;

// OpNode API {{{
class OpNode
{
public:
  virtual ~OpNode() {}

public:
  virtual DataCollectionPtr getOutput(sint pin = 0) = 0;

public:
  virtual OpDesc const*  desc() const                       = 0;
  virtual String         optype() const                     = 0;
  virtual OpGraph*       parent() const                     = 0;
  virtual OpNode*        node(StringView const& name) const = 0;
  virtual String         name() const                       = 0;
  virtual size_t         id() const                         = 0;

  virtual OpContext*  context() const            = 0;
  virtual void        setContext(OpContext* ctx) = 0;
  virtual void        newContext()               = 0;

  virtual bool        isBypassed() const = 0;
  virtual void        setBypassed(bool bypass) = 0;

  virtual void                 setEnv(OpEnvironment const* env) = 0;
  virtual void                 overrideEnv(OpEnvironment env)   = 0;
  virtual OpEnvironment const* env() const                      = 0;

  virtual size_t argCount() const                       = 0;
  virtual sint   argVersion(size_t idx) const           = 0;
  virtual sint   argIndex(StringView const& name) const = 0;
  virtual String argName(sint idx) const                = 0;
  virtual void   evalArgument(StringView const& name)   = 0;
  virtual void   evalAllArguments()                     = 0;

  virtual ArgValue const& arg(sint idx) const = 0;
  virtual ArgValue const& arg(StringView const& name) const = 0;
  virtual ArgValue&       mutArg(StringView const& name) = 0;

  // relations:
  virtual Vector<NodePin> const&                           upstreams() const   = 0;
  virtual Vector<HashSet<NodePin, NodePin::Hasher>> const& downstreams() const = 0;
  virtual void setUpstream(sint inputPin, NodePin const& outputPin)             = 0;
  virtual void addToDownstream(sint outputPin, NodePin const& inputPin)         = 0;
  virtual void removeFromDownstream(sint outputPin, NodePin const& inputPin)    = 0;

  // serialization:
  virtual bool save(Json& doc) const = 0;
  virtual bool load(Json const& doc) = 0;
};
// OpNode API }}}

// OpGraph API {{{
class OpGraph : public OpNode
{
public:
  virtual ~OpGraph() {}

  virtual String                addNode(String const& optype, String const& name) = 0;
  virtual bool                  removeNode(String const& name)                    = 0;
  virtual Vector<String> const& childNames() const                                = 0;
  virtual bool                  renameNode(String const& original, String const& desired, String& accepted) = 0;

  /// link source node's output pin to destiny node's input pin
  /// creating a data pipe
  virtual bool link(String const& srcname, sint srcpin, String const& destname, sint destpin) = 0;
  /// link source node's output pin to destiny node's input pin
  bool link(NodePin const& source, NodePin const& destiny)
  {
    return link(source.name, source.pin, destiny.name, destiny.pin);
  }
  /// link source node's output pin to destiny node's input pin
  bool link(NodeLink const& ld) { return link(ld.source, ld.destiny); }

  /// remove link from (any node) to this dest pin
  /// as one input pin can recieve only one output, it is sufficient to only supply the destiny of
  /// one link
  virtual bool unlink(String const& destname, sint destpin) = 0;
  /// this checks if the link exists, then do the unlink operation
  virtual bool unlink(String const& srcname, sint srcpin, String const& destname, sint destpin) = 0;
  bool         unlink(NodePin const& dest) { return unlink(dest.name, dest.pin); }
  bool         unlink(NodePin const& source, NodePin const& dest)
  {
    return unlink(source.name, source.pin, dest.name, dest.pin);
  }
  bool unlink(NodeLink const& ld) { return unlink(ld.source, ld.destiny); }

  virtual bool setOutputNode(sint pin, String const& name, bool output = true) = 0;

public:
  virtual OpDesc&         mutDesc()                                 = 0;

  /// eval node inplace
  virtual DataCollectionPtr evalNode(String const& name, sint pin = 0) = 0;
};

CORE_API OpGraph* newGraph(String const& name, OpGraph* parent=nullptr);
CORE_API void     deleteGraph(OpGraph* graph);
// OpGraphAPI }}}

// OpGraph Preset Registry {{{
/// Presets are saved graphs
/// very much like HDAs for Houdini
class CORE_API OpGraphPresetRegistry
{
  static OpGraphPresetRegistry* instance_; // mainly for debugging
public:
  /// as the name says, returns an instance of OpGraphPresetRegistry
  static OpGraphPresetRegistry& instance();

  /// register a new preset, named `presetName`, defined by `def`
  /// adding an existing name will replace previous definition
  /// @parm path:         where the preset stores in filesystem
  ///                     can be empty if it's embeded
  /// @parm presetName:   the name of preset
  /// @parm def:          the definition of preset
  /// @parm shared:       can this preset be shared?
  ///                     if false, `create` behaves exactly like `createFolk`
  virtual bool add(String const& path, String const& presetName, Json const& def, bool shared=true) = 0;

  /// query if such preset is defined
  virtual bool registered(String const& presetName) = 0;

  /// scan dir and load / reload all definitions there
  // TODO: scandir
  // void scandir(String const& path) = 0;

  /// create a new instance of preset `name`
  /// the instance is shared and read-only
  /// OpGraphPresetRegistry will keep track of it, so it can
  /// be reloaded
  virtual OpGraph* create(String const& presetName, String const& nodeName) = 0;

  /// create a new editable folk of preset `name`
  virtual OpGraph* createFolk(String const& presetName, String const& nodeName) = 0;

  /// destroy this preset instance
  virtual void destroy(OpGraph* graph) = 0;
};
// }}} Composite Node Type Registry

END_JOYFLOW_NAMESPACE
