#pragma once
#include <spdlog/spdlog.h>
#include <spdlog/async.h>
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <filesystem>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <windows.h> // 실행 파일 경로 추적용

namespace fs = std::filesystem;

class Logger {
public:
    static void Init() {
        try {
            // 1. 실행 파일 절대 경로 구하기
            char buffer[1024];
            if (GetModuleFileNameA(NULL, buffer, 1024) == 0) {
                throw std::runtime_error("Failed to get executable path");
            }
            fs::path exePath(buffer);
            
            // 2. 로그 루트 계산: bin/HiraHelper.exe -> bin -> Root -> logs
            // 즉, 실행파일이 있는 폴더의 형제 폴더인 logs를 타겟팅
            fs::path logRoot = exePath.parent_path().parent_path() / "logs";

            // 3. 날짜별 서브폴더 생성 (logs/YYYY/MM)
            auto now = std::time(nullptr);
            auto tm = *std::localtime(&now);
            std::ostringstream dir_ss;
            dir_ss << std::put_time(&tm, "%Y/%m"); 
            
            fs::path dailyLogDir = logRoot / dir_ss.str();
            if (!fs::exists(dailyLogDir)) fs::create_directories(dailyLogDir);
            
            std::string filePath = (dailyLogDir / "agent.log").string();

            // 4. [Async] Thread Pool 초기화 (Queue Size: 131,072)
            spdlog::init_thread_pool(131072, 1);

            // 5. Sinks 설정
            auto file_sink = std::make_shared<spdlog::sinks::daily_file_sink_mt>(filePath, 0, 0);
            auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            std::vector<spdlog::sink_ptr> sinks {console_sink, file_sink};

            // 6. Async Logger 생성
            auto logger = std::make_shared<spdlog::async_logger>(
                "HiraAgent", 
                sinks.begin(), 
                sinks.end(), 
                spdlog::thread_pool(), 
                spdlog::async_overflow_policy::block 
            );

            logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [t:%t] %v");
            logger->set_level(spdlog::level::info);
            logger->flush_on(spdlog::level::err); 
            spdlog::flush_every(std::chrono::seconds(3));

            spdlog::set_default_logger(logger);
            
            spdlog::info("=== Async Logger Initialized ===");
            spdlog::info("Log Directory: {}", fs::absolute(dailyLogDir).string());

        } catch (const std::exception& ex) {
            fprintf(stderr, "Log Init Failed: %s\n", ex.what());
        }
    }
};