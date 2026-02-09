/**
 * @file ConfigManager.hpp
 * @brief 애플리케이션의 설정을 관리하는 클래스 파일.
 *
 * config.json 파일을 읽어서 메모리(AppConfig 구조체)에 로드하거나,
 * 변경된 설정을 다시 파일로 저장하는 역할을 수행합니다.
 * nlohmann/json 라이브러리를 사용하여 JSON 직렬화/역직렬화를 처리합니다.
 */

#pragma once
#include <fstream>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <filesystem>

// 편의를 위해 nlohmann::json을 json이라는 별칭으로 사용
using json = nlohmann::json;

/**
 * @struct AppConfig
 * @brief 설정 값들을 담고 있는 데이터 구조체
 *
 * 프로그램에서 사용하는 모든 환경 설정 변수를 모아둔 곳입니다.
 * 멤버 변수에 '기본값(Default Value)'을 미리 할당해 두어,
 * 설정 파일이 없거나 초기화되지 않았을 때도 안전한 값을 사용하게 합니다.
 */
struct AppConfig {
    // [Network] NAS 스토리지 IP 설정
    // 기본값은 로컬호스트(127.0.0.1)로 설정하여, 잘못된 연결 시도를 방지함
    std::string nas_short_ip = "127.0.0.1"; // 단기 보관용 NAS IP
    std::string nas_long_ip = "127.0.0.1";  // 장기 보관용 NAS IP

    // [Server] 웹 서버 포트 설정
    // IIS Reverse Proxy가 이 포트(8080)로 트래픽을 전달합니다.
    int port = 8080;

    // [Storage] 로컬 캐시 경로
    // NAS에서 다운로드하거나 업로드된 파일이 임시 저장될 경로입니다.
    // Windows 경로 구분자(\\) 사용에 유의해야 합니다.
    std::string cache_root = "C:\\Temp\\HiraCache";

    // [Phase 4] 디스크 관리(Cleaner) 설정
    bool cleaner_enabled = true;      // 자동 디스크 정리 기능 사용 여부 (true: 켜짐)
    int cleaner_interval_days = 1;    // 디스크 정리 작업 실행 주기 (1일마다 수행)
    int retention_days = 30;          // 파일 보관 기간 (30일이 지난 파일은 삭제 대상)
};

/**
 * @class ConfigManager
 * @brief 설정 로드(Load) 및 저장(Save) 로직을 담당하는 클래스
 */
class ConfigManager {
public:
    AppConfig config;               // 실제 설정 데이터가 담기는 객체
    const std::string file = "config.json"; // 설정 파일의 이름 (실행 파일과 동일 경로 권장)

    /**
     * @brief 생성자
     * 객체가 생성될 때 자동으로 Load()를 호출하여 설정을 초기화합니다.
     */
    ConfigManager() { Load(); }

    /**
     * @brief 설정 파일 로드 함수
     * 1. 파일 존재 여부를 확인합니다.
     * 2. 존재하면 JSON을 파싱하여 AppConfig 변수에 값을 채웁니다.
     * 3. 존재하지 않으면 기본값으로 새 파일을 생성합니다.
     */
    void Load() {
        // C++17 filesystem을 사용하여 파일 존재 여부 확인
        if (std::filesystem::exists(file)) {
            try {
                std::ifstream i(file); // 파일 입력 스트림 열기
                json j;
                i >> j; // JSON 파싱 (텍스트 -> JSON 객체 변환)

                // JSON 객체에서 값을 읽어와 구조체에 저장
                // j.value("key", default_value) 패턴 사용:
                // -> JSON 파일에 해당 "key"가 있으면 그 값을 쓰고,
                // -> 없으면(또는 오타면) 두 번째 인자인 default_value를 사용합니다.
                // 이는 설정 파일 버전이 다르거나 일부 값이 누락되어도 프로그램이 죽지 않게 합니다.

                config.nas_short_ip = j.value("nas_short_ip", config.nas_short_ip);
                config.nas_long_ip = j.value("nas_long_ip", config.nas_long_ip);
                config.port = j.value("port", config.port);
                config.cache_root = j.value("cache_root", config.cache_root);

                // [Phase 4] 신규 추가된 디스크 정리 설정 로드
                config.cleaner_enabled = j.value("cleaner_enabled", true);
                config.cleaner_interval_days = j.value("cleaner_interval_days", 1);
                config.retention_days = j.value("retention_days", 30);

            } catch (...) {
                // JSON 문법 오류 등으로 파싱 실패 시, 로그를 남기고 기본값을 사용
                spdlog::error("Config Load Failed, using defaults");
            }
        } else {
            // 파일이 아예 없으면 현재 설정(기본값)으로 파일을 새로 만듭니다.
            Save();
        }
    }

    /**
     * @brief 설정 파일 저장 함수
     * 현재 메모리 상의 AppConfig 값을 config.json 파일에 씁니다.
     */
    void Save() {
        json j;
        // 구조체 멤버 변수들을 JSON 객체로 매핑
        j["nas_short_ip"] = config.nas_short_ip;
        j["nas_long_ip"] = config.nas_long_ip;
        j["port"] = config.port;
        j["cache_root"] = config.cache_root;
        j["cleaner_enabled"] = config.cleaner_enabled;
        j["cleaner_interval_days"] = config.cleaner_interval_days;
        j["retention_days"] = config.retention_days;

        // 파일 출력 스트림 열기 (기존 내용 덮어쓰기)
        std::ofstream o(file);
        // j.dump(4): JSON을 4칸 들여쓰기(Indentation)하여 사람이 읽기 좋게 저장
        o << j.dump(4);
    }
};