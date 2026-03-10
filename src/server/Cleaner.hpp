/**
 * @file Cleaner.hpp
 * @brief 서버 디스크 공간 관리를 위한 자동 파일 정리 모듈.
 *
 * 백그라운드 스레드에서 지정된 주기(interval)마다 동작하며, 
 * 설정된 보관 기간(retention_days)이 지난 오래된 파일들을 캐시 루트 하위 
 * 전체 폴더(Downloads, Uploads 등)에서 찾아 자동으로 삭제합니다.
 */

#pragma once
#include <thread>
#include <chrono>
#include <filesystem>
#include <spdlog/spdlog.h>
#include "ConfigManager.hpp" 

namespace fs = std::filesystem;

class Cleaner {
public:
    /**
     * @brief 파일 자동 정리 스레드를 구동합니다.
     * * OS에 스레드 관리를 위임(detach)하여 서버의 메인 로직 처리 성능에 
     * 영향을 주지 않으면서 백그라운드에서 주기적으로 동작합니다.
     * * @param cm 서버 설정 관리자 객체 참조 (동적 설정 변경 반영을 위해)
     */
    static void Start(ConfigManager& cm) {
        // 기능 활성화 여부 체크
        if (!cm.config.cleaner_enabled) {
            spdlog::info("[Cleaner] Disabled by config.");
            return;
        }

        std::thread([&cm]() {
            spdlog::info("[Cleaner] Started. Interval: {} days, Retention: {} days", 
                          cm.config.cleaner_interval_days, cm.config.retention_days);
            
            // 데몬(Daemon) 스레드 역할 수행
            while (true) {
                // 설정된 주기(일 단위)만큼 스레드를 슬립(대기) 상태로 전환하여 CPU 자원 소모 방지
                std::this_thread::sleep_for(std::chrono::hours(24 * cm.config.cleaner_interval_days));
                RunCleanup(cm);
            }
        }).detach(); 
    }

private:
    /**
     * @brief 캐시 디렉토리를 순회하며 보존 기간이 만료된 파일을 삭제합니다.
     */
    static void RunCleanup(ConfigManager& cm) {
        spdlog::info("[Cleaner] Scanning for old files...");
        try {
            // 삭제를 결정할 기준 시점(Cut-off) 계산
            auto now = std::chrono::system_clock::now();
            auto cutoff = now - std::chrono::hours(24 * cm.config.retention_days);
            
            fs::path target = cm.config.cache_root;
            if (!fs::exists(target)) return; 

            int deleted_count = 0; 

            // ---------------------------------------------------------
            // [1단계] 기존 로직: 오래된 '파일' 삭제 (이건 원래 있던 로직)
            // ---------------------------------------------------------
            // 지정된 최상위 폴더 이하의 모든 파일 및 서브 폴더를 재귀적으로 스캔
            for (const auto& entry : fs::recursive_directory_iterator(target)) {
                
                // 폴더가 아닌 실제 파일 단위로만 시간 검사 및 삭제 수행
                if (fs::is_regular_file(entry)) {
                    auto ftime = fs::last_write_time(entry);
                    
                    // 파일 시스템 시간을 C++ 시스템 시간 표준(System Clock)으로 안전하게 형변환
                    auto sys_ftime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                        ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now()
                    );

                    // 파일의 마지막 수정 시점이 보존 기준 시점보다 과거라면 삭제 진행
                    if (sys_ftime < cutoff) {
                        fs::remove(entry);
                        deleted_count++;
                        spdlog::debug("[Cleaner] Deleted: {}", entry.path().filename().string());
                    }
                }
            }


            // ---------------------------------------------------------
            // [2단계] 새로 추가: 빈 껍데기 '폴더' 완벽 청소
            // ---------------------------------------------------------
            std::vector<fs::path> empty_dirs;
            // 지우면 안되는 기본 뼈대 폴더 경로 정의
            fs::path base_downloads = target / "Downloads";
            fs::path base_uploads = target / "Uploads";

            // 1. 타겟 폴더 내의 모든 하위 디렉터리 경로를 수집
            for (const auto& entry : fs::recursive_directory_iterator(target)) {
                if (fs::is_directory(entry.status())) {
                    empty_dirs.push_back(entry.path());
                }
            }

            // 2. 경로의 길이(깊이)를 기준으로 내림차순 정렬 
            // -> 가장 깊은 곳에 있는 하위 폴더부터 처리하기 위함
            std::sort(empty_dirs.begin(), empty_dirs.end(), [](const fs::path& a, const fs::path& b) {
                return a.string().length() > b.string().length();
            });

            // 3. 깊은 곳부터 순차적으로 빈 폴더인지 확인하고 삭제
            for (const auto& dir : empty_dirs) {
                try {
                    // 뼈대 폴더(Downloads, Uploads)는 비어있어도 절대 지우지 않음!
                    if (dir == base_downloads || dir == base_uploads || dir == target) {
                        continue; 
                    }

                    // 폴더가 비어있다면 가차없이 삭제
                    if (fs::is_empty(dir)) {
                        fs::remove(dir);
                    }
                } catch (...) {
                    // 만약 누군가(탐색기 등)가 폴더를 열고 있어서 Lock이 걸려있다면,
                    // 프로그램이 죽지 않도록 조용히 넘어가고 다음 청소 주기(내일)에 지웁니다.
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