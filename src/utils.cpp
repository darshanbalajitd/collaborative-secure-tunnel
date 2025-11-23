#include "utils.hpp"

#include <cstdarg>
#include <cstdio>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <fstream>
#include <system_error>

void log_message(const char* level, const char* fmt, ...) {
    fprintf(stderr, "[%s] [%s] ", get_timestamp().c_str(), level);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
}

std::string get_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);

    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %X");
    return ss.str();
}

void initialize_logging(const std::string& path, bool debug) {
    // Minimal stub: create log file if path is provided; debug flag unused here
    if (!path.empty()) {
        std::ofstream ofs(path, std::ios::app);
        if (ofs) {
            ofs << "[" << get_timestamp() << "] [INFO] Logging initialized" << (debug ? " (debug)" : "") << "\n";
        }
    }
}

std::string error_to_string(int errnum) {
    try {
        return std::system_category().message(errnum);
    } catch (...) {
        return "Unknown error";
    }
}
