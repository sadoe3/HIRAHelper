/**
 * @file Logger.hpp
 * @brief 고성능 비동기 로깅 시스템을 초기화하고 관리하는 클래스.
 *
 * Spdlog 라이브러리를 사용하여 콘솔 출력과 일별 파일 저장을 동시에 수행합니다.
 * 메인 스레드의 성능 저하를 방지하기 위해 별도의 스레드에서 로그를 기록하는
 * '비동기(Async)' 방식을 채택했습니다.
 */

#pragma once

// Spdlog 핵심 헤더 및 비동기 기능 헤더
#include <spdlog/spdlog.h>
#include <spdlog/async.h>

// 로그를 파일에 매일 자정마다 새로 생성하여 저장하는 Sink
#include <spdlog/sinks/daily_file_sink.h>

// 콘솔(터미널)에 색상을 입혀 출력하는 Sink
#include <spdlog/sinks/stdout_color_sinks.h>

// 파일 경로 및 시간 처리를 위한 표준 라이브러리
#include <filesystem>
#include <ctime>
#include <iomanip>
#include <sstream>

// [Windows Specific] 실행 파일의 절대 경로를 얻기 위한 헤더
#include <windows.h> 

namespace fs = std::filesystem;

/**
 * @class Logger
 * @brief 로깅 초기화 설정을 담당하는 정적 클래스
 */
class Logger {
public:
    /**
     * @brief 로거 초기화 함수 (프로그램 시작 시 1회 호출 필수)
     * 1. 실행 파일 위치를 파악하여 logs 폴더 경로를 계산합니다.
     * 2. 연/월(YYYY/MM) 구조의 로그 폴더를 생성합니다.
     * 3. 비동기 스레드 풀을 생성하고 로거를 등록합니다.
     */
    static void Init() {
        try {
            // ---------------------------------------------------------
            // 1. 실행 파일(exe)의 절대 경로 구하기
            // ---------------------------------------------------------
            // 윈도우 서비스로 실행될 경우, 작업 경로(Working Dir)가 System32로 잡힐 수 있음.
            // 따라서 반드시 실행 파일 자체의 경로를 기준으로 로그 폴더를 찾아야 함.
            char buffer[1024];
            if (GetModuleFileNameA(NULL, buffer, 1024) == 0) {
                throw std::runtime_error("Failed to get executable path");
            }
            fs::path exePath(buffer);
            
            // ---------------------------------------------------------
            // 2. 로그 저장 루트 경로 계산
            // ---------------------------------------------------------
            // 구조: Root/
            //         ├─ bin/ (HiraHelper.exe 위치)
            //         └─ logs/ (로그 저장 위치)
            // exePath.parent_path() -> bin 폴더
            // .parent_path() 한 번 더 -> Root 폴더
            fs::path logRoot = exePath.parent_path().parent_path() / "logs";

            // ---------------------------------------------------------
            // 3. 날짜별 서브폴더 생성 (구조: logs/YYYY/MM)
            // ---------------------------------------------------------
            // 로그 파일이 한 폴더에 수천 개 쌓이는 것을 방지하기 위해 월별로 격리
            auto now = std::time(nullptr);
            auto tm = *std::localtime(&now);
            
            std::ostringstream dir_ss;
            dir_ss << std::put_time(&tm, "%Y/%m"); // 예: 2026/02
            
            fs::path dailyLogDir = logRoot / dir_ss.str();
            
            // 폴더가 없으면 재귀적으로 생성 (mkdir -p와 동일)
            if (!fs::exists(dailyLogDir)) fs::create_directories(dailyLogDir);
            
            // 최종 로그 파일 전체 경로 (예: C:\HiraAgent\logs\2026\02\agent.log)
            std::string filePath = (dailyLogDir / "agent.log").string();

            // ---------------------------------------------------------
            // 4. [Async] Spdlog Thread Pool 초기화
            // ---------------------------------------------------------
            // Queue Size: 131,072 (2의 17승) - 대량의 로그가 순간적으로 몰려도 버퍼링 가능
            // Thread Count: 1 - 로그 기록 전용 백그라운드 스레드 1개 생성
            spdlog::init_thread_pool(131072, 1);

            // ---------------------------------------------------------
            // 5. Sinks (출력 대상) 설정
            // ---------------------------------------------------------
            // 위에서 계산한 YYYY/MM 경로(filePath) 사용
            auto file_sink = std::make_shared<spdlog::sinks::daily_file_sink_mt>(filePath, 0, 0);            

            // ---------------------------------------------------------
            // 6. Async Logger 생성 및 등록
            // ---------------------------------------------------------
            // 일반 logger가 아니라 고성능 'async_logger'를 유지
            auto logger = std::make_shared<spdlog::async_logger>(
                "HiraAgent",                      // 로거 이름
                file_sink,                        // 오직 파일 싱크만 연결!
                spdlog::thread_pool(),            // 비동기 스레드 풀 사용
                spdlog::async_overflow_policy::block
            );

            // 로그 포맷 설정
            logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [t:%t] %v");
            
            // 로그 레벨 설정 (INFO 등급 '이상' 모두 기록)
            logger->set_level(spdlog::level::info);
            
            // 에러 발생 시 즉시 파일에 씀 (Flush)
            logger->flush_on(spdlog::level::err); 
            
            // 주기적 Flush 설정 (3초마다)
            spdlog::flush_every(std::chrono::seconds(3));

            // 이 로거를 기본 로거로 등록
            spdlog::set_default_logger(logger);
            
            // ---------------------------------------------------------
            // 7. 초기화 완료 로그
            // ---------------------------------------------------------
            spdlog::info("=== Async Logger Initialized ===");
            // 실제 로그가 저장되는 절대 경로를 출력하여 디버깅 용이하게 함
            spdlog::info("Log Directory: {}", fs::absolute(dailyLogDir).string());

        } catch (const std::exception& ex) {
            // 로거 초기화 실패 시, 표준 에러(stderr)로 출력하고 프로그램 진행
            // (여기서 죽으면 원인 파악이 힘드므로 최소한의 흔적을 남김)
            fprintf(stderr, "Log Init Failed: %s\n", ex.what());
        }
    }
};