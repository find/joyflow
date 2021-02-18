#pragma once
#include "def.h"
#include <spdlog/spdlog.h>

BEGIN_JOYFLOW_NAMESPACE

void CORE_API setLogger(std::shared_ptr<spdlog::logger> logger);

END_JOYFLOW_NAMESPACE

