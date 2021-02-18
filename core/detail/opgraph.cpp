#include "../def.h"

#include "opgraph_detail.h"
#include "linearmap.h"
#include "runtime.h"
#include "serialize.h"

#include <spdlog/spdlog.h>
#include <fmt/format.h>
#include <nlohmann/json.hpp>

BEGIN_JOYFLOW_NAMESPACE

// OpNode Impl {{{
OpNodeImpl::OpNodeImpl(String const& name, OpGraphImpl* parent, OpDesc const* desc)
    : desc_(desc), name_(name), parent_(parent), id_(Runtime::allocNodeID())
{
  if (desc) {
    for (auto const& argdesc : desc->argDescs) {
      argValues_.insert(argdesc.name, ArgValue(&argdesc, nullptr));
    }
  }
  spdlog::info("Node \"{}\"({}) created", name, (void*)(this));
}

OpNodeImpl::~OpNodeImpl()
{
  spdlog::info("Node \"{}\"({}) destroyed", name_, (void*)(this));
}

OpDesc const* OpNodeImpl::desc() const
{
  return desc_;
}

void OpNodeImpl::setUpstream(sint inputPin, NodePin const& outputPin)
{
  RUNTIME_CHECK(inputPin >= 0, "inputPin ({}) < 0", inputPin);
  RUNTIME_CHECK(inputPin < desc_->numMaxInput,
                "inputPin ({}) >= numMaxInput({})",
                inputPin,
                desc_->numMaxInput);
  ensureVectorSize(upstreams_, inputPin + 1);
  upstreams_[inputPin] = outputPin;

  if (context_)
    context_->markInputDirty(inputPin, true);
}

void OpNodeImpl::addToDownstream(sint outputPin, NodePin const& inputPin)
{
  RUNTIME_CHECK(outputPin >= 0, "outputPin ({}) < 0", outputPin);
  RUNTIME_CHECK(outputPin < desc_->numOutputs,
                "outputPin ({}) >= numOutputs({})",
                outputPin,
                desc_->numOutputs);
  ensureVectorSize(downstreams_, outputPin + 1);
  downstreams_[outputPin].insert(inputPin);
}

void OpNodeImpl::removeFromDownstream(sint outputPin, NodePin const& inputPin)
{
  RUNTIME_CHECK(outputPin >= 0, "outputPin ({}) < 0", outputPin);
  RUNTIME_CHECK(outputPin < desc_->numOutputs,
                "outputPin ({}) >= numOutputs({})",
                outputPin,
                desc_->numOutputs);
  ensureVectorSize(downstreams_, outputPin + 1);
  downstreams_[outputPin].erase(inputPin);
}

DataCollectionPtr OpNodeImpl::getOutput(sint pin)
{
  RUNTIME_CHECK(
      context_, "{}: make sure you have called prepareEvaluation() before getOutput()", name_);
  try {
    return context_->getOrCalculateOutputData(pin);
  } catch (std::exception const& e) {
    (void)e;
    return nullptr;
  }
}

ArgValue const& OpNodeImpl::arg(sint idx) const
{
  static const ArgDesc noneExistArgDesc = {
      ArgType::REAL,           // type
      "none_exist",            // name
      "None Exist",            // label
      1,                       // tuple size
      "YOU SHALL BE CAREFUL!", // description
      {"0", "0", "0", "0"}     // default expressions
  };
  static const ArgValue defaultArgValue(&noneExistArgDesc, nullptr);
  if (idx != -1)
    return argValues_[idx];
  else
    return defaultArgValue;
}

ArgValue& OpNodeImpl::mutArg(StringView const& name)
{
  String strname(name);
  size_t idx = argValues_.indexof(strname);
  if (idx == -1) {
    ArgValue newVal(nullptr, nullptr);
    newVal.mutDesc().name = strname;
    idx                   = argValues_.insert(strname, newVal);
  }
  return argValues_[idx];
}

void OpNodeImpl::evalArgument(StringView const& name)
{
  auto* parg = argValues_.find(name);
  if (parg) {
    parg->eval(context_.get());
  }
}

void OpNodeImpl::evalAllArguments()
{
  for (auto& arg : argValues_) {
    arg.eval(context_.get());
  }
}

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(NodePin, name, pin);

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(NodeLink, source, destiny);

bool OpNodeImpl::save(Json& self) const
{
  self["type"]        = optype();
  self["upstreams"]   = upstreams();
  self["downstreams"] = downstreams();
  self["bypassed"]    = isBypassed();
  auto& args          = self["args"];
  for (auto const& argname : argNames()) {
    auto const& argv = arg(argname);
    argv.save(args[argname]);
  }
  return true;
}

bool OpNodeImpl::load(Json const& self)
{
  if (auto stddesc = OpRegistry::instance().get(self["type"]))
    desc_ = stddesc;
  else
    spdlog::warn("description for type \"{}\" cannot be found", self["type"]);
  if (auto bpsitr = self.find("bypassed"); bpsitr != self.end())
    setBypassed(bool(*bpsitr));
  else
    setBypassed(false);

  for (auto const& pin : self["upstreams"]) {
    upstreams_.push_back(NodePin(pin));
  }
  for (auto const& pin : self["downstreams"]) {
    HashSet<NodePin, NodePin::Hasher> pinConnections;
    for (auto const& con : pin) {
      pinConnections.insert(NodePin(con));
    }
    downstreams_.push_back(std::move(pinConnections));
  }
  for (auto const& [name, arg] : self["args"].items()) {
    mutArg(std::string(name)).load(arg);
  }
  return true;
}
// OpNode Impl }}}

// OpGraph Impl {{{

OpGraphImpl::OpGraphImpl(String const& name, OpGraph* parent)
    : OpNodeImpl(name, static_cast<OpGraphImpl*>(parent), new OpDesc)
{
  ownDesc_  = const_cast<OpDesc*>(desc_); // safe because we've just alloc this thing
  *ownDesc_ = OpDescBuilder()
                  .name("subnet")
                  .numRequiredInput(0)
                  .numMaxInput(4)
                  .numOutputs(1)
                  .icon(/*ICON_FA_NETWORK_WIRED*/ "\xEF\x9B\xBF")
                  .get();
}

OpGraphImpl::~OpGraphImpl()
{
  for (auto* cnode : children_) {
    delete cnode;
  }
  delete ownDesc_;
}

String OpGraphImpl::addNode(String const& optype, String const& name)
{
  auto* opdef = OpRegistry::instance().get(optype);
  // RUNTIME_CHECK(opdef, "opdef({}) == nullptr", optype);
  if (!opdef) {
    spdlog::error("cannot find definition for op \"{}\"", optype);
    opdef = OpRegistry::instance().get("missing");
  }
  String realname = name;
  for (; children_.find(realname) != nullptr; realname = increaseNumericSuffix(realname))
    ;

  // TODO: factory?
  OpNode* child = nullptr;

  if (optype == "subnet") // TODO: dirty! rework this!
    child = newGraph(optype, this);
  else if (opdef)
    child = new OpNodeImpl(realname, this, opdef);
  else
    child = OpGraphPresetRegistry::instance().create(optype, realname);

  children_.insert(realname, child);
  return realname;
}

bool OpGraphImpl::removeNode(String const& name)
{
  OpNode* torm = node(name);
  if (!torm)
    return false;
  RUNTIME_CHECK(torm->name() == name, "name mismatch??");
  sint pin = 0;
  for (NodePin const& ipin : torm->upstreams()) {
    if (ipin.isValid()) {
      RUNTIME_CHECK(node(ipin.name), "node {} cannot be found", ipin.name);
      node(ipin.name)->removeFromDownstream(ipin.pin, NodePin{name, pin});
    }
    ++pin;
  }
  for (HashSet<NodePin, NodePin::Hasher> const& opins : torm->downstreams()) {
    for (NodePin const& opin : opins) {
      RUNTIME_CHECK(node(opin.name), "node {} cannot be found", opin.name);
      node(opin.name)->setUpstream(opin.pin, NodePin());
    }
  }
  // TODO factory?
  delete children_.remove(name);
  children_.tighten();
  return true;
}

bool OpGraphImpl::renameNode(String const& original, String const& desired, String& accepted)
{
  auto id = children_.indexof(original);
  if (id == -1)
    return false;
  auto*  opnode  = static_cast<OpNodeImpl*>(children_[id]);
  String newname = desired;
  // TODO: configurable count limit?
  for (size_t trycnt = 0; trycnt < 1000 && children_.find(newname) != nullptr;
       newname       = increaseNumericSuffix(newname))
    ++trycnt;
  if (children_.find(newname) != nullptr) {
    spdlog::warn(
        "after 1000 trys, still failed to rename node {} to {}, now give up", original, desired);
    return false;
  }

  // re-connect links
  for (sint i = 0, n = opnode->upstreams().size(); i < n; ++i) {
    auto pinup = opnode->upstreams()[i];
    if (!pinup.isValid())
      continue;
    auto* upnode = node(pinup.name);
    ASSERT(upnode);
    upnode->removeFromDownstream(pinup.pin, NodePin{original, i});
    upnode->addToDownstream(pinup.pin, NodePin{newname, i});
  }
  for (sint i = 0, n = opnode->downstreams().size(); i < n; ++i) {
    auto const& downlinks = opnode->downstreams()[i];
    for (auto const& pindown : downlinks) {
      auto* downnode = node(pindown.name);
      ASSERT(downnode);
      downnode->setUpstream(pindown.pin, NodePin{newname, i});
    }
  }

  opnode->setName(newname);
  children_.reset(id, newname, opnode);
  accepted = newname;
  spdlog::info("node \"{}\"({}) renamed to {}", original, (void*)this, accepted);
  return true;
}

bool OpGraphImpl::link(String const& srcname, sint srcpin, String const& dstname, sint dstpin)
{
  auto* src = node(srcname);
  auto* dst = node(dstname);

  if (!src || !dst)
    return false;

  if (src->desc()->numOutputs <= srcpin)
    return false;

  if (dst->desc()->numMaxInput <= dstpin)
    return false;

  unlink(dstname, dstpin);
  dst->setUpstream(dstpin, NodePin{srcname, srcpin});
  if (auto* ctx = dst->context()) {
    ctx->resetInput(dstpin);
  }
  src->addToDownstream(srcpin, NodePin{dstname, dstpin});
  return true;
}

bool OpGraphImpl::unlink(String const& dstname, sint dstpin)
{
  auto* dst = node(dstname);
  if (!dst)
    return false;

  auto const&   ups    = dst->upstreams();
  NodePin const srcpin = dstpin >= 0 && ups.ssize() > dstpin ? ups[dstpin] : NodePin();
  if (srcpin.isValid()) {
    auto* src = node(srcpin.name);
    if (src) {
      NodePin me{dstname, dstpin};
      src->removeFromDownstream(srcpin.pin, me);
    }
  }
  dst->setUpstream(dstpin, NodePin());
  if (auto* ctx = dst->context()) {
    ctx->resetInput(dstpin);
  }
  return true;
}

bool OpGraphImpl::unlink(String const& srcname, sint srcpin, String const& dstname, sint dstpin)
{
  auto* src = node(srcname);
  auto* dst = node(dstname);

  if (!src || !dst)
    return false;

  auto const&   ups        = dst->upstreams();
  NodePin const srcnodepin = dstpin >= 0 && ups.ssize() > dstpin ? ups[dstpin] : NodePin();
  if (srcnodepin.name == srcname && srcnodepin.pin == srcpin) {
    NodePin me{dstname, dstpin};
    src->removeFromDownstream(srcpin, me);
  }
  dst->setUpstream(dstpin, NodePin());
  if (dst->context())
    dst->context()->markInputDirty(dstpin);
  return true;
}

void OpGraphImpl::prepareEvaluation(String const& nodeToResolve)
{
  PROFILER_SCOPE("prepareEvaluation", 0xBDDD22);
  // first pass:
  // - mark used nodes
  // - check if input count satisfies requirement
  // - check if infinite loop exists
  // - marks active output pins
  HashSet<OpNode*> visitedNodes;
  Vector<OpNode*>  edgeNodes;
  Vector<OpNode*>  dfsOrder; // order of depth-first visit, parent always before children
  Vector<OpNode*>  dstNodes; // output nodes

  auto* theOnlyOutput = node(nodeToResolve);

  if (theOnlyOutput) {
    dstNodes.push_back(theOnlyOutput);
  } else {
    for (auto id : outputNodes_)
      dstNodes.push_back(children_[id]);
  }

  RUNTIME_CHECK(!dstNodes.empty(), "No output node specified");

  edgeNodes = dstNodes;
  // depth-first search from output nodes
  while (!edgeNodes.empty()) {
    OpNode* top = edgeNodes.back();
    edgeNodes.pop_back();

    if (visitedNodes.find(top) != visitedNodes.end()) {
      continue;
    }
    visitedNodes.insert(top);

    for (auto pin : top->upstreams()) {
      if (pin.isValid()) {
        auto* edge = node(pin.name);
        RUNTIME_CHECK(edge, "node {} cannot be found", pin.name);
        edgeNodes.push_back(edge);
      }
    }
    dfsOrder.push_back(top);
  }

  // check loop
  {
    std::map<Pair<OpNode*, sint>, HashSet<OpNode*>> dependencies;
    for (auto ritr = dfsOrder.rbegin(), rend = dfsOrder.rend(); ritr != rend; ++ritr) {
      // inverse order ensures upstreams are resolved before downstream
      OpNode* me = *ritr;
      for (sint pidx = 0, npins = me->upstreams().size(); pidx < npins; ++pidx) {
        auto&   pin = me->upstreams()[pidx];
        OpNode* up  = node(pin.name);
        if (!up)
          continue;
        auto& pindeps = dependencies[{me, pidx}];
        pindeps.insert(up);
        for (sint uppidx = 0, nuppins = up->upstreams().size(); uppidx < nuppins; ++uppidx) {
          auto const& updeps = dependencies[{up, uppidx}];
          pindeps.insert(updeps.begin(), updeps.end());
        }

        if (pindeps.find(me) != pindeps.end()) {
          bool thisLoopIsAllowed = false;
          for (auto dep : pindeps) {
            auto flg = dep->desc()->flags;
            if (!(flg & OpFlag::ALLOW_LOOP))
              continue;
            sint meAtPin = -1;
            bool foundMe = false;
            for (meAtPin = 0; meAtPin < uint32_t(OpFlag::LOOPPIN_MAXCOUNT); ++meAtPin) {
              if (meAtPin >= dep->upstreams().ssize())
                break;
              auto const& deppindeps = dependencies[{dep, meAtPin}];
              if (deppindeps.find(me) != deppindeps.end()) {
                foundMe = true;
                break;
              }
            }
            if (foundMe && !!(flg & OpFlag(1 << (uint32_t(OpFlag::LOOPPIN_BITSHIFT) + meAtPin))))
              thisLoopIsAllowed = true;
          }
          if (!thisLoopIsAllowed)
            throw ExecutionError(fmt::format("found loop {0} -> {0}", me->name()));
        }
      }
    }
  }

  // update related nodes' evaluation context(s)
  for (OpNode* vn : visitedNodes) {
    if (vn->context() == nullptr) {
      auto* ctx = newOpContext(vn);
      vn->setContext(ctx);
    }
    vn->context()->bindKernel();
  }

  // output nodes' always output through pin 0
  for (auto* nd : dstNodes)
    nd->context()->setOutputActive(0, true);
  // mark dependencies' output active
  for (auto* vn : visitedNodes) {
    for (auto const& pin : vn->upstreams()) {
      if (!pin.isValid()) // disconnected pin
        continue;
      auto* context = node(pin.name)->context();
      RUNTIME_CHECK(context, "(node {}).context == nullptr", pin.name);
      context->setOutputActive(pin.pin, true);
    }
  }

  // call beforeFrameEval in leaf -> root order
  for (auto itr = dfsOrder.rbegin(); itr != dfsOrder.rend(); ++itr) {
    OpNode* vn = *itr;
    vn->context()->beforeFrameEval();
  }

  // call eval on arguments
  for (auto itr = dfsOrder.rbegin(); itr != dfsOrder.rend(); ++itr) {
    OpNode* vn = *itr;
    vn->context()->evalArguments();
    if (vn->context()->inputDirty() || vn->context()->argDirty() ||
        vn->context()->outputActivityDirty()) {
      vn->context()->markDirty(true);
      for (auto const& pinset : vn->downstreams()) {
        for (auto const& pin : pinset) {
          if (auto* dsnode = node(pin.name)) {
            if (auto* ctx = dsnode->context()) {
              ctx->markInputDirty(pin.pin, true);
            }
          }
        }
      }
      vn->context()->setScheduled(false);
    }
  }

  // second pass: mark data dirty
  // - maybe we don't really need this
  // for (OpNode* vn : visitedNodes) {
  //  if (vn->context()->inputDirty() || vn->context()->argDirty()) {
  //    HashSet<OpNode*> downstreamVisited;
  //    Vector<OpNode*>  downstreamEdge;
  //    downstreamEdge.push_back(vn);
  //    while (!downstreamEdge.empty()) {
  //      OpNode* edgenode = downstreamEdge.back();
  //      downstreamEdge.pop_back();
  //      for (auto const& pinset : edgenode->downstreams()) {
  //        for (NodePin const& pin : pinset) {
  //          OpNode* dsnode = node(pin.name);
  //          THROW_IF_NOT(dsnode, "node {} does not exist", pin.name);
  //          // if this downstream node is not connected to output, then skip the branch
  //          if (dsnode->context()) {
  //            dsnode->context()->markInputDirty(pin.pin, true);
  //            if (downstreamVisited.find(dsnode) == downstreamVisited.end()) {
  //              downstreamEdge.push_back(dsnode);
  //              downstreamVisited.insert(dsnode);
  //            }
  //          }
  //        }
  //      }
  //    }
  //  }
  //}
}

void OpGraphImpl::cleanupEvaluation()
{
  for (auto* child : children_) {
    if (auto* ctx = child->context())
      ctx->afterFrameEval();
  }
}

void OpGraphImpl::GraphEval::eval(OpContext& context) const
{
  auto* graph = static_cast<OpGraphImpl*>(static_cast<OpNodeImpl*>(context.node()));
  if (graph == nullptr || (graph->childNames().empty() && graph->desc()->numMaxInput > 0 &&
                           graph->desc()->numOutputs > 0)) {
    for (sint opin = 0; opin < graph->desc()->numOutputs; ++opin)
      context.copyInputToOutput(opin, 0);
    return;
  }
  // beforeFrameEval will be called within prepareEvaluation()
  graph->prepareEvaluation();
  for (size_t i = 0, n = graph->outputNodes_.size(); i < n; ++i) {
    sint pin = static_cast<sint>(i);
    ASSERT(pin == i); // check not overflowing
    if (graph->context_->outputIsActive(pin)) {
      // graph_->getOutput(i);
      if (graph->outputNodes_[i] == -1 || graph->outputNodes_[i] > graph->children_.size()) {
        context.setOutputData(pin, nullptr);
      } else {
        auto output = graph->children_[graph->outputNodes_[i]]->getOutput();
        // no need to share - the output itself should already be an cache
        /*
        auto share = output->share();
        context.setOutputData(i, share.get());
        */
        context.setOutputData(pin, output);
      }
    }
  }
  graph->cleanupEvaluation();
}

static struct GraphEvalOpRegister
{
  GraphEvalOpRegister()
  {
    OpRegistry::instance().add(makeOpDesc<OpGraphImpl::GraphEval>("subnet")
                                   .numRequiredInput(0)
                                   .numMaxInput(4)
                                   .numOutputs(1)
                                   .icon(/*ICON_FA_NETWORK_WIRED*/ "\xEF\x9B\xBF"));
  }
} graphEvalOpReg_ = {};

DataCollectionPtr OpGraphImpl::evalNode(String const& name, sint pin)
{
  auto* outnode = node(name);
  if (outnode == nullptr)
    return nullptr;
  if (outnode->context()) // if the context already exists, this will mark output activity dirty
    outnode->context()->setOutputActive(pin, true);
  prepareEvaluation(name);
  outnode->context()->setOutputActive(pin, true);
  auto dc = outnode->getOutput(pin);
  cleanupEvaluation();
  return dc;
}

// void OpGraphImpl::evaluate()
// {
//   THROW_IF_NOT(context_, "make sure to call prepareEvaluation before evaluating");
//   for (sint i=0, n=desc()->numOutputs; i<n; ++i) {
//     if (context_->outputIsActive(i))
//       children_[outputNodes_[i]]->evaluate();
//   }
// }

DataCollectionPtr OpGraphImpl::getOutput(sint pin)
{
  if (pin >= 0 && pin < outputNodes_.ssize()) {
    if (outputNodes_[pin] == -1 || outputNodes_[pin] > children_.size()) {
      return nullptr;
    } else {
      auto onode = outputNodes_[pin];
      return evalNode(children_[onode]->name(), 0);
    }
  }
  return nullptr;
}

bool OpGraphImpl::setOutputNode(sint pin, String const& name, bool output)
{
  RUNTIME_CHECK(0 <= pin && pin < ownDesc_->numOutputs,
                "pin {} out of range [0-{})",
                pin,
                ownDesc_->numOutputs);
  ensureVectorSize(outputNodes_, pin + 1, -1);
  if (output) {
    outputNodes_[pin] = children_.indexof(name);
    return true;
  } else {
    outputNodes_[pin] = -1;
    return true;
  }
}

CORE_API OpGraph* newGraph(String const& name, OpGraph* parent)
{
  return new OpGraphImpl(name, parent);
}

CORE_API void deleteGraph(OpGraph* graph)
{
  delete graph;
}

bool OpGraphImpl::load(const Json& self)
{
  // ----------- clean up --------------
  for (auto* node : children_) {
    delete node;
  }
  children_.clear();
  outputNodes_.clear();

  // ----------- read in --------------

  // TODO: don't load desc_ for opnode?
  if (!OpNodeImpl::load(self))
    return false;
  if (self.find("desc") != self.end()) {
    *ownDesc_ = self["desc"];
  } else {
    *ownDesc_ = OpDescBuilder(*ownDesc_)
                    .name("subnet")
                    .numMaxInput(4)
                    .numRequiredInput(0)
                    .numOutputs(1)
                    .icon(/*ICON_FA_NETWORK_WIRED*/ "\xEF\x9B\xBF");
  }
  desc_                   = ownDesc_;
  auto const& childrensec = self["children"];
  bool        succeed     = true;
  for (auto const& child : childrensec.items()) {
    String type     = child.value()["type"];
    String name     = child.key();
    auto   realname = this->addNode(type, name);
    if (realname != name) {
      spdlog::error(
          "node {} of type {} renamed to {}, this should not happen", name, type, realname);
      succeed = false;
    }
    if (!this->node(realname)->load(child.value())) {
      spdlog::error("loading node {} of type {} failed", name, type);
      succeed = false;
    }
  }
  for (auto const& outid : self["outputs"]) {
    outputNodes_.push_back(outid);
  }
  return succeed;
}

bool OpGraphImpl::save(Json& self) const
{
  if (!OpNodeImpl::save(self))
    return false;
  self["desc"]      = *ownDesc_;
  auto& childrensec = self["children"];
  bool  succeed     = true;
  for (auto const* child : children_) {
    succeed &= child->save(childrensec[child->name()]);
  }
  self["outputs"] = outputNodes_;
  return succeed;
}

// OpGraph Impl }}}

// OpGraphPresetRegistry Impl {{{
OpGraphPresetRegistry* OpGraphPresetRegistry::instance_ = nullptr;

OpGraphPresetRegistry& OpGraphPresetRegistry::instance()
{
  static OpGraphPresetRegistry* reginstance = new OpGraphPresetRegistryImpl;
  if (instance_ == nullptr)
    instance_ = reginstance;
  return *instance_;
}

bool OpGraphPresetRegistryImpl::add(String const& path,
                                    String const& presetName,
                                    Json const&   def,
                                    bool          shared)
{
  PresetDefinition pdef    = {path, std::move(def), path.empty(), shared};
  definitions_[presetName] = std::move(pdef);
  return true;
}

bool OpGraphPresetRegistryImpl::registered(String const& presetName)
{
  return definitions_.find(presetName) != definitions_.end();
}

OpGraph* OpGraphPresetRegistryImpl::create(String const& presetName, String const& nodeName)
{
  // TODO: shared instance
  return createFolk(presetName, nodeName);
}

OpGraph* OpGraphPresetRegistryImpl::createFolk(String const& presetName, String const& nodeName)
{
  // TODO: create folk
  auto itr = definitions_.find(presetName);
  if (itr != definitions_.end()) {
    OpGraph* graph = newGraph(nodeName);
    graph->load(itr->second.definition);
    return graph;
  }
  return newGraph(nodeName);
}

void OpGraphPresetRegistryImpl::destroy(OpGraph* graph)
{
  // TODO: destroy graph
  deleteGraph(graph);
}
// OpGraphPresetRegistry Impl }}}

// Object Inspector {{{
static struct OpNodeInspectorRegister
{
  OpNodeInspectorRegister()
  {
    ObjectInspector inspector = {/*name*/
                                 [](void const* obj) -> String {
                                   auto const* node = static_cast<OpNodeImpl const*>(obj);
                                   return node->name();
                                 },
                                 /*size*/
                                 [](void const* obj) -> size_t {
                                   auto const* node = static_cast<OpNodeImpl const*>(obj);
                                   return node->countMemory();
                                 }};

    Stats::setInspector<OpNodeImpl>(inspector);
    Stats::setInspector<OpGraphImpl>(inspector);
  }
} _reg;
// Object Inspector }}}

END_JOYFLOW_NAMESPACE
