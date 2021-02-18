#define  DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>
#ifdef _WIN32
#include <spdlog/sinks/wincolor_sink.h>
#include <spdlog/sinks/msvc_sink.h>
#else
#include <spdlog/sinks/ansicolor_sink.h>
#endif
#include "core/log.h"

int main(int argc, char** argv)
{
#ifdef _WIN32
  auto logger = std::make_shared<spdlog::logger>("", std::make_shared<spdlog::sinks::wincolor_stdout_sink_mt>());
  logger->sinks().emplace_back(std::make_shared<spdlog::sinks::msvc_sink_mt>());
#else
  auto logger = std::make_shared<spdlog::logger>("", std::make_shared<spdlog::sinks::ansicolor_stdout_sink_mt>());
#endif
  logger->set_level(spdlog::level::trace);
  joyflow::setLogger(logger);
  return doctest::Context(argc, argv).run();
}

