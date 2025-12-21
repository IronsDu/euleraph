#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/daily_file_sink.h>

static void initialize_log(spdlog::level::level_enum log_level = spdlog::level::info)
{
    const std::string log_pattern = "[%H:%M:%S.%3f %z] [%^%l%$] [thread %t] %v";

    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto daily_sink   = std::make_shared<spdlog::sinks::daily_file_sink_mt>("logs/daily.txt", 0, 0);

    std::vector<spdlog::sink_ptr> sinks{console_sink, daily_sink};
    for (auto& sink : sinks)
    {
        sink->set_level(log_level);
        sink->set_pattern(log_pattern);
    }

    auto logger = std::make_shared<spdlog::logger>("multi_sink", sinks.begin(), sinks.end());
    spdlog::register_logger(logger);
    logger->set_level(log_level);

    spdlog::set_default_logger(logger);
}