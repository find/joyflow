#include "../opcontext.h"
#include "../datatable.h"
#include "../profiler.h"

#include "opgraph_detail.h"

#include <mutex>

BEGIN_JOYFLOW_NAMESPACE

class RootContextImpl: public RootContext
{
private:
  using NodeId = size_t;

  OpGraph*   root_ = nullptr;
  HashMap<NodeId, std::unique_ptr<OpContext>> nodeContexts_;
  HashMap<StringView, NodeId> nodeIds_;
  HashMap<NodeId, OpNode*> allNodes_;
  HashSet<OpNode*> visitedNodes_;
  HashSet<NodeId> goals_;
  Vector<NodeId> dfsOrder_;
  std::mutex mutex_;

public:
  OVERRIDE_NEW_DELETE

  void bind(OpGraph* root) override;
  void unbind() override;
  void addGoal(StringView oppath) override; // ?
  void eval() override;
  DataCollectionPtr fetch(StringView oppath, sint pin) override;

private:
  void lock() { mutex_.lock(); }
  void unlock() { mutex_.unlock(); }

  void clear();
  void resolve(HashSet<NodeId> const& goals, String const& rootPath="/"); // visit the network, resolve `visitedNodes_` and `dfsOrder_` from `goals_` and checks for loop
  void prepare(); // prepare for evaluation, init `nodeContexts_`
  void addSubnet(OpGraph* subnet, String cwd);
  NodeId getIdFromPath(String const& cwd, String const& nodename) const;
};


void RootContextImpl::clear()
{
  nodeContexts_.clear();
  nodeIds_.clear();
  allNodes_.clear();
  visitedNodes_.clear();
  dfsOrder_.clear();
  goals_.clear();
}

void RootContextImpl::addSubnet(OpGraph* subnet, String cwd)
{
  nodeIds_[cwd] = subnet->id();
  allNodes_[subnet->id()] = subnet;
  for (auto const& name: subnet->childNames()) {
    auto* node = subnet->node(name);
    ALWAYS_ASSERT(node!=nullptr);
    if (auto subsubnet = dynamic_cast<OpGraph*>(node)) {
      addSubnet(subsubnet, cwd + node->name() + "/");
    } else {
      nodeIds_[cwd + node->name()] = node->id();
      allNodes_[node->id()] = node;
    }
  }
}

void RootContextImpl::bind(OpGraph* root)
{
  std::lock_guard<std::mutex> guard(mutex_);
  clear();
  root_ = root;
  addSubnet(root, "/");
}

void RootContextImpl::unbind()
{
  std::lock_guard<std::mutex> guard(mutex_);
  clear();
  root_ = nullptr;
}

void RootContextImpl::addGoal(StringView oppath)
{
  std::lock_guard<std::mutex> guard(mutex_);
  if (auto iditr = nodeIds_.find(oppath); iditr != nodeIds_.end()) {
    goals_.insert(iditr->second);
  }
}

void RootContextImpl::eval()
{
  std::lock_guard<std::mutex> guard(mutex_);
  for (auto id : goals_) {
    nodeContexts_.at(id)->schedule();
  }
}

DataCollectionPtr RootContextImpl::fetch(StringView oppath, sint pin)
{
  std::lock_guard<std::mutex> guard(mutex_);
  ALWAYS_ASSERT(goals_.find(nodeIds_[oppath]) != goals_.end());

  return nodeContexts_.at(nodeIds_[oppath])->getOutputCache(pin);
}

RootContextImpl::NodeId RootContextImpl::getIdFromPath(String const& cwd, String const& nodename) const
{
  String path = cwd.substr(0, cwd.find_last_of('/'));
  if (path.back() != '/')
    path += '/';
  path += nodename;
  return nodeIds_.at(path);
}

void RootContextImpl::resolve(HashSet<NodeId> const& goals, String const& rootPath)
{
  std::lock_guard<std::mutex> guard(mutex_);
  PROFILER_SCOPE("resolve", 0xBD3322);

  Vector<OpNode*>  edgeNodes;

  RUNTIME_CHECK(!goals.empty(), "No output node specified");

  //edgeNodes.assign(goals_.begin(), goals_.end());
  edgeNodes.resize(goals.size());
  std::transform(goals.begin(), goals.end(), edgeNodes.begin(), [this](NodeId id) {return allNodes_.at(id); });
  // depth-first search from output nodes
  while (!edgeNodes.empty()) {
    OpNode* top = edgeNodes.back();
    edgeNodes.pop_back();

    if (visitedNodes_.find(top) != visitedNodes_.end()) {
      continue;
    }
    visitedNodes_.insert(top);

    for (auto pin : top->upstreams()) {
      if (pin.isValid()) {
        auto* edge = allNodes_.at(getIdFromPath(rootPath, pin.name));
        RUNTIME_CHECK(edge, "node {} cannot be found", pin.name);
        if (auto subnet = dynamic_cast<OpGraph*>(edge)) {
          // resolve({}, rootPath + subnet->name() + "/"); // TODO: resolve subnet
        } else {
          edgeNodes.push_back(edge);
        }
      }
    }
    dfsOrder_.push_back(top->id());
  }

  // check loop
  {
    std::map<Pair<OpNode*, sint>, HashSet<OpNode*>> dependencies;
    for (auto ritr = dfsOrder_.rbegin(), rend = dfsOrder_.rend(); ritr != rend; ++ritr) {
      // inverse order ensures upstreams are resolved before downstream
      OpNode* me = allNodes_.at(*ritr);
      for (sint pidx = 0, npins = me->upstreams().size(); pidx < npins; ++pidx) {
        auto& pin = me->upstreams()[pidx];
        OpNode* up = allNodes_.at(getIdFromPath(rootPath, pin.name));
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
}

void RootContextImpl::prepare()
{
  std::lock_guard<std::mutex> guard(mutex_);
  PROFILER_SCOPE("prepareEvaluation", 0xBDDD22);

#if 0 // TODO
  // update related nodes' evaluation context(s)
  for (OpNode* vn : visitedNodes_) {
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
#endif // TODO
}

RootContext* newRootContext()
{
  return new RootContextImpl;
}


END_JOYFLOW_NAMESPACE

