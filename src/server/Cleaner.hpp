#pragma once
#include <thread>
#include <chrono>
#include <filesystem>
#include <spdlog/spdlog.h>
#include "ConfigManager.hpp"

namespace fs = std::filesystem;

class Cleaner {
public:
    static void Start(ConfigManager& cm) {
        if (!cm.config.cleaner_enabled) {
            spdlog::info("[Cleaner] Disabled by config.");
            return;
        }

        // 백그라운드 스레드 실행 (Detach)
        std::thread([&cm]() {
            spdlog::info("[Cleaner] Started. Interval: {} days, Retention: {} days", 
                         cm.config.cleaner_interval_days, cm.config.retention_days);
            
            while (true) {
                // 설정된 기간만큼 대기 (Day -> Hours 변환)
                std::this_thread::sleep_for(std::chrono::hours(24 * cm.config.cleaner_interval_days));
                
                RunCleanup(cm);
            }
        }).detach();
    }

private:
    static void RunCleanup(ConfigManager& cm) {
        spdlog::info("[Cleaner] Scanning for old files...");
        try {
            auto now = std::chrono::system_clock::now();
            auto cutoff = now - std::chrono::hours(24 * cm.config.retention_days);
            
            fs::path target = cm.config.cache_root;
            if (!fs::exists(target)) return;

            int deleted_count = 0;
            // 재귀적으로 탐색
            for (const auto& entry : fs::recursive_directory_iterator(target)) {
                if (fs::is_regular_file(entry)) {
                    auto ftime = fs::last_write_time(entry);
                    
                    // C++20 시계 변환
                    auto sys_ftime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                        ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now()
                    );

                    if (sys_ftime < cutoff) {
                        fs::remove(entry);
                        deleted_count++;
                        spdlog::debug("[Cleaner] Deleted: {}", entry.path().filename().string());
                    }
                }
            }
            if (deleted_count > 0) {
                spdlog::info("[Cleaner] Completed. Deleted {} files.", deleted_count);
            } else {
                spdlog::info("[Cleaner] Nothing to delete.");
            }

        } catch (const std::exception& e) {
            spdlog::error("[Cleaner] Error: {}", e.what());
        }
    }
};