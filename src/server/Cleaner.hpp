/**
 * @file Cleaner.hpp
 * @brief 디스크 공간 관리를 위한 자동 파일 정리 모듈.
 *
 * 백그라운드 스레드에서 주기적으로 동작하며, 설정된 보관 기간(retention_days)이
 * 지난 파일들을 캐시 폴더에서 자동으로 삭제하여 디스크 용량 부족을 방지합니다.
 */

#pragma once
#include <thread>
#include <chrono>
#include <filesystem>
#include <spdlog/spdlog.h>
#include "ConfigManager.hpp" // 설정값(보관 주기 등)을 읽기 위해 포함

namespace fs = std::filesystem;

/**
 * @class Cleaner
 * @brief 주기적인 파일 정리 작업을 수행하는 정적 유틸리티 클래스
 */
class Cleaner {
public:
    /**
     * @brief 클리너 스레드 시작 함수
     * 메인 스레드와 분리된(Detached) 백그라운드 스레드를 생성하여
     * 주기적으로 RunCleanup()을 실행합니다.
     * * @param cm 설정 관리자 객체 (설정값 참조용)
     */
    static void Start(ConfigManager& cm) {
        // 1. 설정에서 기능이 꺼져있으면(false) 즉시 종료
        if (!cm.config.cleaner_enabled) {
            spdlog::info("[Cleaner] Disabled by config.");
            return;
        }

        // 2. 백그라운드 스레드 생성 (람다 함수 실행)
        // cm 객체는 참조로 캡처하여 최신 설정값을 계속 읽을 수 있게 함
        std::thread([&cm]() {
            // 시작 로그: 설정된 주기와 보관 기간 출력
            spdlog::info("[Cleaner] Started. Interval: {} days, Retention: {} days", 
                          cm.config.cleaner_interval_days, cm.config.retention_days);
            
            // 무한 루프: 프로그램이 종료될 때까지 계속 동작
            while (true) {
                // 설정된 주기(일 단위)를 시간 단위로 변환하여 대기 (Sleep)
                // 예: 1일 -> 24시간 대기. 이 동안은 CPU를 쓰지 않음 (Blocked)
                std::this_thread::sleep_for(std::chrono::hours(24 * cm.config.cleaner_interval_days));
                
                // 대기가 끝나면 청소 작업 실행
                // (참고: 실제 프로덕션에서는 sleep 대신 condition_variable을 써서
                //  프로그램 종료 시 즉시 깨어날 수 있게 만드는 것이 더 좋지만, 
                //  현재 구조에서는 간단한 sleep으로 구현됨)
                RunCleanup(cm);
            }
        }).detach(); // 3. 메인 스레드와 분리 (Detach)
        // detach()를 호출하면 메인 함수가 끝나도 스레드 객체 소멸 시 에러가 나지 않으며,
        // OS가 스레드 수명을 관리하게 됨 (데몬 스레드처럼 동작)
    }

private:
    /**
     * @brief 실제 파일 삭제 로직을 수행하는 함수
     * 캐시 폴더를 순회하며 오래된 파일을 찾아 삭제합니다.
     */
    static void RunCleanup(ConfigManager& cm) {
        spdlog::info("[Cleaner] Scanning for old files...");
        try {
            // 1. 기준 시간 계산 (Cutoff Time)
            // 현재 시간 - 보관 기간(예: 30일) = 삭제 기준 시점
            // 이 시점보다 이전에 수정된 파일은 삭제 대상임
            auto now = std::chrono::system_clock::now();
            auto cutoff = now - std::chrono::hours(24 * cm.config.retention_days);
            
            // 2. 대상 폴더 확인
            fs::path target = cm.config.cache_root;
            if (!fs::exists(target)) return; // 폴더가 없으면 할 일 없음

            int deleted_count = 0; // 삭제된 파일 수 카운터

            // 3. 재귀적 탐색 (Recursive Iterator)
            // 하위 폴더까지 모두 뒤져서 파일 하나하나 검사
            for (const auto& entry : fs::recursive_directory_iterator(target)) {
                
                // 디렉토리가 아닌 '일반 파일'인 경우만 처리
                if (fs::is_regular_file(entry)) {
                    // 파일의 마지막 수정 시간(Last Write Time) 가져오기
                    auto ftime = fs::last_write_time(entry);
                    
                    // [C++20 Time Conversion]
                    // 파일 시스템 시간(file_time_type)을 시스템 시간(system_clock)으로 변환
                    // (컴파일러마다 file_clock과 system_clock의 에포크가 다를 수 있어 보정 필요)
                    auto sys_ftime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                        ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now()
                    );

                    // 4. 삭제 판단 및 실행
                    // 파일 수정 시간이 기준 시점(cutoff)보다 과거라면? -> 삭제
                    if (sys_ftime < cutoff) {
                        fs::remove(entry); // 파일 삭제
                        deleted_count++;
                        // 디버그 레벨 로그 (파일이 많을 수 있으므로 info 대신 debug 권장)
                        spdlog::debug("[Cleaner] Deleted: {}", entry.path().filename().string());
                    }
                }
            }
            
            // 5. 결과 리포팅
            if (deleted_count > 0) {
                spdlog::info("[Cleaner] Completed. Deleted {} files.", deleted_count);
            } else {
                spdlog::info("[Cleaner] Nothing to delete.");
            }

        } catch (const std::exception& e) {
            // 권한 문제나 파일 잠금 등으로 에러 발생 시 로그 남기고 계속 진행
            spdlog::error("[Cleaner] Error: {}", e.what());
        }
    }
};