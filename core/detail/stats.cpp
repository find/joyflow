#include "stats.h"
#include "error.h"
#include <cstdint>
#include <phmap.h>

BEGIN_JOYFLOW_NAMESPACE

namespace detail {

struct ObjectAllocationInfo
{
  String      objectName;
  void const* address = nullptr;
  size_t      size    = 0;
  // clock_t      birthTime;
  // clock_t      deadTime;
};

struct TypeIndexHash // for vs2017 capacity
{
  size_t operator()(std::type_index const& ti) const { return ti.hash_code(); }
};

class StatsDetail
{
  static StatsDetail*       instance_;
  mutable std::shared_mutex mutex_;
  mutable std::shared_mutex inspectorMutex_;
  using allallocmap_t = phmap::flat_hash_map<std::type_index, size_t, TypeIndexHash>;
  using clsobjcount_t = phmap::flat_hash_map<std::type_index, size_t, TypeIndexHash>;
  using clsobjlist_t  = phmap::flat_hash_map<std::type_index, phmap::node_hash_set<void const*>, TypeIndexHash>;
  using inspectormap_t = phmap::flat_hash_map<std::type_index, ObjectInspector, TypeIndexHash>;

  allallocmap_t  allocCounts_;
  clsobjcount_t  clsLivingCounts_;
  clsobjlist_t   clsLivingObjects_;
  inspectormap_t inspectors_;

public:
  static StatsDetail& instance()
  {
    static std::unique_ptr<StatsDetail> s_instance(new StatsDetail);
    if (instance_ == nullptr)
      instance_ = s_instance.get();
    return *instance_;
  }

  void add(std::type_index typeIndex, void const* address, size_t size)
  {
    std::unique_lock<std::shared_mutex> guard(mutex_);
    ++allocCounts_[typeIndex];
    ++clsLivingCounts_[typeIndex];
    clsLivingObjects_[typeIndex].insert(address);
  }

  void remove(std::type_index typeIndex, void const* address)
  {
    std::unique_lock<std::shared_mutex> guard(mutex_);
    ALWAYS_ASSERT(--clsLivingCounts_[typeIndex]!=-1);
    clsLivingObjects_[typeIndex].erase(address);
  }

  void dumpLiving(FILE* file)
  {
    std::shared_lock<std::shared_mutex> guard(mutex_);
    bool anything_printed = false;
    for (auto const& cls: clsLivingCounts_) {
      if (cls.second != 0) {
        fprintf(file, "class \"%s\": %zd objects living\n", cls.first.name(), cls.second);
        bool hasInspector = false;
        ObjectInspector inspector = {};
        {
          std::shared_lock<std::shared_mutex> inspectorlock(inspectorMutex_);
          auto itr = inspectors_.find(cls.first);
          if (itr!=inspectors_.end()) {
            inspector = itr->second;
            hasInspector = true;
          }
        }
        if (hasInspector) {
          for (auto const* ptr: clsLivingObjects_[cls.first]) {
            std::string fmtstring = "";
            if (inspector.name)
              fmtstring += "\"" + inspector.name(ptr) + "\": ";
            if (inspector.sizeInBytes)
              fmtstring += std::to_string(inspector.sizeInBytes(ptr)) + "bytes";
            if (inspector.sizeInBytesShared && inspector.sizeInBytesUnshared) {
              fmtstring += "  (" + std::to_string(inspector.sizeInBytesShared(ptr)) + "b shared, "
                          + std::to_string(inspector.sizeInBytesUnshared(ptr)) + "b unshared)";
            }
            fprintf(file, "    %p: %s\n", ptr, fmtstring.c_str());
          }
        }
        anything_printed = true;
      }
    }
    if (!anything_printed)
      fprintf(file, "everything clean.\n");
  }

  void dumpLiving(void(*dumpf)(char const* msg, void* arg), void* arg)
  {
    std::shared_lock<std::shared_mutex> guard(mutex_);
    bool anything_printed = false;
    for (auto const& cls: clsLivingCounts_) {
      if (cls.second != 0) {
        dumpf(fmt::format("class \"{}\": {} objects living\n", cls.first.name(), cls.second).c_str(), arg);
        bool hasInspector = false;
        ObjectInspector inspector = {};
        {
          std::shared_lock<std::shared_mutex> inspectorlock(inspectorMutex_);
          auto itr = inspectors_.find(cls.first);
          if (itr!=inspectors_.end()) {
            inspector = itr->second;
            hasInspector = true;
          }
        }
        if (hasInspector) {
          for (auto const* ptr: clsLivingObjects_[cls.first]) {
            std::string fmtstring = "";
            if (inspector.name)
              fmtstring += "\"" + inspector.name(ptr) + "\": ";
            if (inspector.sizeInBytes)
              fmtstring += std::to_string(inspector.sizeInBytes(ptr)) + "bytes";
            if (inspector.sizeInBytesShared && inspector.sizeInBytesUnshared) {
              fmtstring += "  (" + std::to_string(inspector.sizeInBytesShared(ptr)) + "b shared, "
                          + std::to_string(inspector.sizeInBytesUnshared(ptr)) + "b unshared)";
            }
            fmtstring = fmt::format("    {}: {}", ptr, fmtstring);
            dumpf(fmtstring.c_str(), arg);
          }
        }
        anything_printed = true;
      }
    }
    if (!anything_printed)
      dumpf("everything clean.", arg);
  }

  size_t totalAllocCount() const
  {
    std::shared_lock<std::shared_mutex> guard(mutex_);
    size_t sum = 0;
    for (auto const& cls : allocCounts_) {
      sum += cls.second;
    }
    return sum;
  }

  size_t totalAllocCount(std::type_index typeIndex) const
  {
    std::shared_lock<std::shared_mutex> guard(mutex_);
    if (auto itr = allocCounts_.find(typeIndex); itr != allocCounts_.end())
      return itr->second;
    else
      return 0;
  }

  size_t livingCount() const
  {
    std::shared_lock<std::shared_mutex> guard(mutex_);
    size_t sum = 0;
    for (auto const& cls : clsLivingCounts_) {
      sum += cls.second;
    }
    return sum;
  }

  size_t livingCount(std::type_index typeIndex) const
  {
    std::shared_lock<std::shared_mutex> guard(mutex_);
    if (auto itr = clsLivingCounts_.find(typeIndex); itr != clsLivingCounts_.end())
      return itr->second;
    else
      return 0;
  }

  void setInspector(std::type_index typeIndex, ObjectInspector const& inspector)
  {
    std::unique_lock<std::shared_mutex> guard(inspectorMutex_);
    inspectors_[typeIndex] = inspector;
  }
};

StatsDetail* StatsDetail::instance_ = nullptr;

} // namespace detail

void Stats::add(std::type_index typeIndex,
                void const*     address,
                size_t          size)
{
  detail::StatsDetail::instance().add(typeIndex, address, size);
}

void Stats::remove(std::type_index typeIndex, void const* address)
{
  detail::StatsDetail::instance().remove(typeIndex, address);
}

void Stats::dumpLiving(FILE* file)
{
  detail::StatsDetail::instance().dumpLiving(file);
}

void Stats::dumpLiving(void(*dumpf)(char const* msg, void* arg), void* arg)
{
  detail::StatsDetail::instance().dumpLiving(dumpf, arg);
}

size_t Stats::totalAllocCount()
{
  return detail::StatsDetail::instance().totalAllocCount();
}

size_t Stats::totalAllocCount(std::type_index typeIndex)
{
  return detail::StatsDetail::instance().totalAllocCount(typeIndex);
}

size_t Stats::livingCount()
{
  return detail::StatsDetail::instance().livingCount();
}

size_t Stats::livingCount(std::type_index typeIndex)
{
  return detail::StatsDetail::instance().livingCount(typeIndex);
}

void Stats::setInspector(std::type_index typeIndex, ObjectInspector const& inspector)
{
  detail::StatsDetail::instance().setInspector(typeIndex, inspector);
}

END_JOYFLOW_NAMESPACE
