#include "runtime.h"
#include "opdesc.h"
#include <atomic>

BEGIN_JOYFLOW_NAMESPACE

sint Runtime::allocDataID()
{
  static std::atomic<sint> counter = 1;
  return ++counter;
}

uint64_t Runtime::allocNodeID()
{
  static std::atomic<uint64_t> counter = 1;
  return ++counter;
}

TaskContext& TaskContext::instance()
{
  static std::unique_ptr<TaskContext> instance_{ new TaskContext(marl::Scheduler::Config::allCores()) };
  return *instance_;
}

END_JOYFLOW_NAMESPACE
