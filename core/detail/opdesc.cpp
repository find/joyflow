#include "opdesc.h"
#include "serialize.h"

BEGIN_JOYFLOW_NAMESPACE

struct OpKernelHandleHash
{
  size_t operator()(OpKernelHandle const& h) const { return std::hash<void const*>()(*h); }
};
struct OpKernelHandleEq
{
  bool operator()(OpKernelHandle const& a, OpKernelHandle const& b) const { return *a == *b; }
};

class OpRegistryImpl : public OpRegistry
{
private:
  OpRegistryImpl() {}
  OpRegistryImpl(OpRegistryImpl const&); // don't copy me
  HashMap<String, HashSet<OpKernelHandle, OpKernelHandleHash, OpKernelHandleEq>> opInstances_;
  HashMap<String, OpDesc>                                                        descRegistery_;
  static OpRegistryImpl*                                                         instance_;

public:
  static OpRegistryImpl& instance();

  OpDesc const* get(String const& name) const override
  {
    auto itr = descRegistery_.find(name);
    if (itr == descRegistery_.end()) {
      return nullptr;
    } else {
      return &itr->second;
    }
  }

  Vector<String> list() const override;

  bool add(OpDesc const& desc, bool overwriteExisting = true) override;

  void remove(String const& name) override;
  void remove(OpDesc const& desc) override;
  bool replace(String const& name, OpDesc const& newDesc) override;

  OpKernelHandle createOp(String const& name) override;
  void           destroyOp(OpKernelHandle op) override;
};

OpRegistryImpl* OpRegistryImpl::instance_ = nullptr;

OpRegistryImpl& OpRegistryImpl::instance()
{
  static std::unique_ptr<OpRegistryImpl> s_instance(new OpRegistryImpl());
  if (instance_ == nullptr)
    instance_ = s_instance.get();
  return *instance_;
}

bool OpRegistryImpl::add(OpDesc const& desc, bool overwriteExisting)
{
  auto itr            = descRegistery_.find(desc.name);
  bool shouldRecreate = false;
  if (itr != descRegistery_.end() && overwriteExisting) {
    remove(itr->second);
    shouldRecreate = true;
  }
  descRegistery_[desc.name] = desc;
  if (shouldRecreate) {
    // TODO: overwrite OpDesc
  }
  return true;
}

Vector<String> OpRegistryImpl::list() const
{
  Vector<String> result;
  for(auto const& record: descRegistery_) {
    result.push_back(record.first);
  }
  return result;
}

void OpRegistryImpl::remove(String const& name)
{
  // TODO: remove OpDesc
}

void OpRegistryImpl::remove(OpDesc const& desc)
{
  // TODO: remove OpDesc
}

bool OpRegistryImpl::replace(String const& name, OpDesc const& newDesc)
{
  // TODO: replace OpDesc
  return false;
}

OpKernelHandle OpRegistryImpl::createOp(String const& name)
{
  auto itr = descRegistery_.find(name);
  if (itr != descRegistery_.end()) {
    ALWAYS_ASSERT(itr->second.createKernel != nullptr);
    ALWAYS_ASSERT(itr->second.destroyKernel != nullptr);
    auto* kernel = itr->second.createKernel();
    if (!kernel)
      return nullptr;
    OpKernelHandle handle(kernel);
    opInstances_[name].insert(handle);
    return handle;
  } else {
    return nullptr;
  }
}

void OpRegistryImpl::destroyOp(OpKernelHandle op)
{
  // TODO: destroy op
  delete *op;
  //auto itr=descRegistery_.find();
  //if (itr!=descRegistery_.end())
  //  itr->destroy(op);
}

OpRegistry& OpRegistry::instance()
{
  return OpRegistryImpl::instance();
}

END_JOYFLOW_NAMESPACE
