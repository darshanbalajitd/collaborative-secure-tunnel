#pragma once

#include <string>

#define LOG_INFO(fmt, ...) log_message("INFO", fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) log_message("ERROR", fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...) log_message("WARN", fmt, ##__VA_ARGS__)

void log_message(const char* level, const char* fmt, ...);

std::string get_timestamp();

void initialize_logging(const std::string& path, bool debug);

std::string error_to_string(int errnum);
