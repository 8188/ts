#ifndef MYLOGGER_H
#define MYLOGGER_H

#include "spdlog/async.h"
#include "spdlog/spdlog.h"
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

void init_logger();

#endif // MYLOGGER_H