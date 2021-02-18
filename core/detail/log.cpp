#include "log.h"
#include <spdlog/spdlog.h>

BEGIN_JOYFLOW_NAMESPACE

void setLogger(std::shared_ptr<spdlog::logger> logger)
{
  spdlog::set_default_logger(logger);
}

END_JOYFLOW_NAMESPACE
