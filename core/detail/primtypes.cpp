#include "primtypes.h"
#include "linearmap.h"
#include <memory>

BEGIN_JOYFLOW_NAMESPACE

class PrimTypeRegistryImpl : public PrimTypeRegistery
{
  using UnderlyingTypeEnum = std::underlying_type_t<DataType>;
  LinearMap<std::type_index, PrimTypeDefinition> defs_;
  HashMap<DataType, size_t> luts_;
  UnderlyingTypeEnum typeEnumCounter_ = 0;
public:
  DataType add(std::type_index type, PrimTypeDefinition const& def) override
  {
    if (auto ptr=defs_.find(type)) {
      return ptr->typeEnum;
    } else {
      DataType typeEnum = static_cast<DataType>(
        static_cast<UnderlyingTypeEnum>(DataType::CUSTOM) +
        ++typeEnumCounter_
      );
      PrimTypeDefinition mutDef = def;
      mutDef.typeEnum = typeEnum;

      auto id = defs_.insert(type, mutDef);
      luts_[typeEnum] = id;
      return typeEnum;
    }
  }

  DataType getDataType(std::type_index type) const override
  {
    if (auto ptr=defs_.find(type)) {
      return ptr->typeEnum;
    } else {
      return DataType::UNKNOWN;
    }
  }

  PrimTypeDefinition const* getDefinition(std::type_index type) const override
  {
    if (auto ptr=defs_.find(type)) {
      return ptr;
    } else {
      return nullptr;
    }
  }

  PrimTypeDefinition const* getDefinition(DataType typeEnum) const override
  {
    if (auto itr=luts_.find(typeEnum); itr!=luts_.end()) {
      return &defs_[itr->second];
    }
    return nullptr;
  }
};

PrimTypeRegistery& PrimTypeRegistery::instance()
{
  static std::unique_ptr<PrimTypeRegistryImpl> instance_{ new PrimTypeRegistryImpl() };
  return *instance_.get();
}

END_JOYFLOW_NAMESPACE
