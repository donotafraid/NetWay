#include "OPCUAPacking/ua.h"
#include "spdlog/spdlog.h"
#include "spdlog/sinks/rotating_file_sink.h"  // 修复 rotating_logger_mt 错误
#include "spdlog/sinks/stdout_color_sinks.h"  // 修复 stderr_color_mt 错误

int main(int argc, char *argv[]) {
    try {
        // 用 rotating + mt 版本，才是真正的"滚动 + 线程安全"
        auto logger = spdlog::rotating_logger_mt(
            "global_logger",
            "logs/app.log",
            1024 * 1024 * 5,   // 5MB
            3);                // 保留 3 个

        logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
        logger->set_level(spdlog::level::debug);
        logger->flush_on(spdlog::level::warn);

        spdlog::set_default_logger(logger);
    } catch (const spdlog::spdlog_ex &e) {
        std::fprintf(stderr, "spdlog init failed: %s\n", e.what());
        auto console = spdlog::stderr_color_mt("console");
        spdlog::set_default_logger(console);
    }

    spdlog::info("全局日志系统已启动！");

    spdlog::shutdown();   // 退出前 flush 所有 logger
    return 0;
}

