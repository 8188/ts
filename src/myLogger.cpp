#include "myLogger.h"

void init_logger()
{
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_level(spdlog::level::trace);

    auto rotating_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>("logs/async_log.txt", 1024 * 1024 * 10, 10, false);
    console_sink->set_level(spdlog::level::info);

    spdlog::init_thread_pool(8192, 1);
    auto async_logger = std::make_shared<spdlog::async_logger>("async_logger", spdlog::sinks_init_list { console_sink, rotating_sink }, spdlog::thread_pool(), spdlog::async_overflow_policy::block);
    spdlog::set_default_logger(async_logger);

    spdlog::flush_every(std::chrono::seconds(3));
    spdlog::flush_on(spdlog::level::warn);
}
