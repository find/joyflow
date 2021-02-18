#pragma once

#include "opgraph.h"

#include <phmap.h>

#include <marl/event.h>
#include <marl/task.h>
#include <marl/scheduler.h>
#include <marl/waitgroup.h>

#include <atomic>

BEGIN_JOYFLOW_NAMESPACE

class Runtime
{
public:
  static sint     allocDataID();
  static uint64_t allocNodeID();
};

class TaskContext
{
  TaskContext(const marl::Scheduler::Config& cfg): scheduler(cfg) { scheduler.bind(); }
public:
  ~TaskContext() {
    scheduler.unbind();
  }
  marl::Scheduler scheduler;

  static TaskContext& instance();
};

END_JOYFLOW_NAMESPACE
