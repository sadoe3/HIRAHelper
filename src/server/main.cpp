/**
 * @file main.cpp
 * @brief HIRA Helper Agent의 메인 진입점 및 서버 로직 구현 파일.
 * * Crow 프레임워크를 사용하여 RESTful API 서버를 구동하며,
 * PACS 데이터 전송, 파일 업로드/다운로드, 설정 관리, 디스크 정리 등의 핵심 기능을 제공합니다.
 * IIS Reverse Proxy 환경에서 동작하도록 설계되었으며, UTF-8 경로 처리를 지원합니다.
 */

#include "crow.h"
#include "StorageHandler.hpp"
#include "ConfigManager.hpp"
#include "HtmlTemplates.hpp"
#include "Logger.hpp"
#include "Cleaner.hpp"
#include <sstream>

/**
 * @brief URL 인코딩된 문자열을 디코딩하는 함수.
 * * 웹 요청에서 전달받은 URL 인코딩된 파라미터(예: %20 -> 공백)를
 * 일반 문자열로 변환합니다.
 * * @param value 인코딩된 문자열
 * @return std::string 디코딩된 문자열
 */
std::string UrlDecode(const std::string& value) {
    std::string result;
    result.reserve(value.length());
    for (size_t i = 0; i < value.length(); ++i) {
        if (value[i] == '+') {
            result += ' '; // '+' 기호는 공백으로 변환
        } else if (value[i] == '%' && i + 2 < value.length()) {
            // '%' 뒤의 두 글자를 16진수로 해석하여 문자로 변환
            std::string hex = value.substr(i + 1, 2);
            char chr = (char)std::strtol(hex.c_str(), nullptr, 16);
            result += chr;
            i += 2;
        } else {
            result += value[i]; // 그 외 문자는 그대로 추가
        }
    }
    return result;
}

int main() {
    // 1. 로깅 시스템 초기화 (Spdlog 비동기 설정 등)
    Logger::Init();

    // 2. 설정 파일(config.json) 로드
    ConfigManager cm;
    spdlog::info("=== HIRA Helper Agent v2.9.2 (Robust Server) Started ===");

    // 3. 디스크 정리 스레드(Cleaner) 시작 (설정된 주기마다 오래된 파일 삭제)
    Cleaner::Start(cm);

    // 4. 캐시 폴더가 없으면 생성 (파일 다운로드/업로드 경로)
    std::filesystem::create_directories(cm.config.cache_root);

    // 5. Crow 앱 인스턴스 생성
    crow::SimpleApp app;

    // =========================================================
    // [Config Route] 설정 페이지 (GET)
    // =========================================================
    // 웹 브라우저에서 현재 설정을 확인하는 HTML 페이지를 제공합니다.
    CROW_ROUTE(app, "/config")([&]() {
        std::string html = CONFIG_HTML; // HtmlTemplates.hpp에 정의된 템플릿 사용

        // 템플릿 문자열 치환 람다 함수
        auto replace = [&](std::string& str, const std::string& key, const std::string& val) {
            size_t pos = 0;
            while ((pos = str.find(key, pos)) != std::string::npos) {
                str.replace(pos, key.length(), val);
                pos += val.length();
            }
        };

        // 현재 설정값으로 HTML 템플릿 채우기
        replace(html, "{{NAS_SHORT}}", cm.config.nas_short_ip);
        replace(html, "{{NAS_LONG}}", cm.config.nas_long_ip);
        replace(html, "{{PORT}}", std::to_string(cm.config.port));
        replace(html, "{{CACHE_ROOT}}", cm.config.cache_root);
        replace(html, "{{RETENTION}}", std::to_string(cm.config.retention_days));
        replace(html, "{{INTERVAL}}", std::to_string(cm.config.cleaner_interval_days));
        replace(html, "{{CLEANER_CHECKED}}", cm.config.cleaner_enabled ? "checked" : "");
        
        return html;
    });

    // =========================================================
    // [Config Route] 설정 저장 (POST)
    // =========================================================
    // 웹 페이지 폼에서 전송된 변경된 설정을 파싱하여 저장합니다.
    CROW_ROUTE(app, "/config").methods(crow::HTTPMethod::POST)
    ([&](const crow::request& req) {
        bool new_cleaner_enabled = false;
        std::stringstream ss(req.body);
        std::string segment;

        // application/x-www-form-urlencoded 데이터 파싱
        while (std::getline(ss, segment, '&')) {
            size_t splitPos = segment.find('=');
            if (splitPos != std::string::npos) {
                std::string key = segment.substr(0, splitPos);
                // 값은 URL Decoding 필요 (한글/특수문자 처리)
                std::string val = UrlDecode(segment.substr(splitPos + 1));

                // 각 설정 키에 맞춰 값 업데이트
                if (key == "nas_short_ip") cm.config.nas_short_ip = val;
                else if (key == "nas_long_ip") cm.config.nas_long_ip = val;
                else if (key == "port") try { cm.config.port = std::stoi(val); } catch(...) {}
                else if (key == "cache_root") cm.config.cache_root = val;
                else if (key == "retention_days") try { cm.config.retention_days = std::stoi(val); } catch(...) {}
                else if (key == "cleaner_interval_days") try { cm.config.cleaner_interval_days = std::stoi(val); } catch(...) {}
                else if (key == "cleaner_enabled") new_cleaner_enabled = true; // 체크박스 존재 시 true
            }
        }
        
        // Cleaner Enabled 값 업데이트 및 설정 파일 저장
        cm.config.cleaner_enabled = new_cleaner_enabled;
        cm.Save();

        // 저장 완료 응답
        return crow::response(200, "<h1>Saved!</h1><p>Restart required.</p><a href='/config'>Back</a>");
    });

    // =========================================================
    // [PACS Route] NAS 폴더 다운로드 (POST)
    // =========================================================
    // NAS의 특정 폴더를 로컬 캐시 폴더로 복사합니다.
    // Strict Routing: PacsShort/PacsLong 접두사 검증을 통해 보안을 강화합니다.
    CROW_ROUTE(app, "/pacs/download").methods(crow::HTTPMethod::POST)
    ([&](const crow::request& req) {
        auto x = crow::json::load(req.body);
        if (!x || !x.has("target_path")) return crow::response(400, "Invalid JSON");
        
        std::string target = x["target_path"].s(); // 클라이언트가 요청한 경로 (예: PacsShort\Study01)
        std::string target_ip;
        std::string relative_path;

        // [Strict Routing Logic]
        // 요청 경로가 허용된 접두사(PacsShort, PacsLong)로 시작하는지 검사
        if (target.rfind("PacsShort", 0) == 0) { 
            target_ip = cm.config.nas_short_ip;
            // "PacsShort\" (10글자) 제거하여 실제 NAS 내부 상대 경로 추출
            if (target.length() > 9) relative_path = target.substr(10);
            else relative_path = ""; 
        } 
        else if (target.rfind("PacsLong", 0) == 0) {
            target_ip = cm.config.nas_long_ip;
            // "PacsLong\" (9글자) 제거
            if (target.length() > 8) relative_path = target.substr(9);
            else relative_path = "";
        }
        else {
            // 허용되지 않은 경로는 차단 (Security)
            spdlog::warn("[Req] Blocked Invalid Prefix: {}", target);
            return crow::response(400, "Invalid Path Prefix");
        }

        // 실제 NAS UNC 경로 조립 (예: \\192.168.10.50\Study01)
        std::string unc_path = "\\\\" + target_ip + "\\" + relative_path;
        
        spdlog::info("[Req] NAS Download: {} -> UNC: {}", target, unc_path);
        
        std::string local_path;
        // StorageHandler를 통해 폴더 다운로드 수행 (재귀적 복사)
        if (StorageHandler::DownloadFolder(unc_path, cm.config.cache_root, local_path)) {
            crow::json::wvalue res;
            res["status"] = "success";
            res["local_path"] = local_path;
            return crow::response(200, res);
        }
        return crow::response(500, "Download Failed");
    });

    // =========================================================
    // [PACS Route] 로컬 폴더 삭제 (POST)
    // =========================================================
    // 캐시 폴더 내의 특정 하위 폴더를 삭제합니다.
    CROW_ROUTE(app, "/pacs/delete").methods(crow::HTTPMethod::POST)
    ([&](const crow::request& req) {
        auto x = crow::json::load(req.body);
        if (!x || !x.has("target_path")) return crow::response(400, "Missing 'target_path'");
        
        std::string target = x["target_path"].s();
        // 삭제 대상 전체 경로 생성 (CacheRoot + Target)
        // fs::path 조합 시 StorageHandler::ToPath 사용은 DeleteFolder 내부에서 처리됨
        fs::path full_path = fs::path(cm.config.cache_root) / target; 

        if (StorageHandler::DeleteFolder(full_path.string())) { 
            crow::json::wvalue res;
            res["status"] = "deleted";
            return crow::response(200, res);
        }
        return crow::response(404, "Folder not found");
    });

    // =========================================================
    // [File Route] 파일 업로드 (POST)
    // =========================================================
    // 클라이언트로부터 파일을 받아 로컬 캐시 폴더 내 Uploads 폴더에 저장합니다.
    // Multipart Form Data 파싱 및 UTF-8 파일명 처리를 지원합니다.
    CROW_ROUTE(app, "/file/upload").methods(crow::HTTPMethod::POST)
    ([&](const crow::request& req) {
        crow::multipart::message msg(req);
        int saved_count = 0;
        // 기본 업로드 경로는 CacheRoot/Uploads
        std::string upload_dir = (fs::path(cm.config.cache_root) / "Uploads").string();

        for (const auto& part : msg.parts) {
            if (!part.body.empty()) {
                // Content-Disposition 헤더에서 파일명 추출
                auto it = part.headers.find("Content-Disposition");
                if (it != part.headers.end()) {
                    auto& params = it->second.params;
                    auto filename_it = params.find("filename");
                    
                    if (filename_it != params.end()) {
                        std::string raw_name = filename_it->second;
                        // 따옴표 제거 (예: "file.jpg" -> file.jpg)
                        if (raw_name.size() >= 2 && raw_name.front() == '"' && raw_name.back() == '"') {
                            raw_name = raw_name.substr(1, raw_name.size() - 2);
                        }
                        
                        // [UTF-8 Support] 
                        // StorageHandler::SaveFileAtomic 내부에서 u8path 변환을 수행하므로
                        // 여기서는 std::string(UTF-8) 그대로 전달합니다.
                        spdlog::info("[Req] File Upload: {}", raw_name);
                        
                        // 임시 파일 생성 -> 이동(Rename) 방식의 Atomic Write 수행
                        if (StorageHandler::SaveFileAtomic(upload_dir, raw_name, part.body)) {
                            saved_count++;
                        }
                    }
                }
            }
        }
        
        if (saved_count > 0) {
            crow::json::wvalue res;
            res["status"] = "success";
            res["saved_count"] = saved_count;
            return crow::response(200, res);
        }
        return crow::response(400, "No valid files found");
    });

    // =========================================================
    // [File Route] 파일 다운로드 (GET)
    // =========================================================
    // 로컬 캐시 폴더 내의 파일을 스트림 방식으로 클라이언트에게 전송합니다.
    // 한글 파일명 깨짐 방지 및 대용량 파일 처리를 지원합니다.
    CROW_ROUTE(app, "/file/download").methods(crow::HTTPMethod::GET)
    ([&](const crow::request& req) {
        auto path_param = req.url_params.get("path"); // 요청 예: /file/download?path=Uploads/test.jpg
        if (!path_param) return crow::response(400, "Missing 'path' parameter");

        std::string full_path;
        
        // 1. 파일 유효성 검사 및 경로 획득
        // GetFileForDownload는 PathTraversal 공격을 방어하고, 
        // 결과값(full_path)으로 안전한 UTF-8 문자열(PathToStr 결과)을 반환합니다.
        if (StorageHandler::GetFileForDownload(cm.config.cache_root, path_param, full_path)) {
            
            // 2. 파일 열기 (Binary 모드)
            // UTF-8 문자열을 Windows Path로 변환(ToPath)하여 엽니다.
            std::ifstream file(StorageHandler::ToPath(full_path), std::ios::binary);
            if (!file.is_open()) return crow::response(500, "File open error");

            // 파일 내용을 스트림 버퍼에 담음 (메모리 효율적 방식 고려 가능하나 현재는 전체 로드)
            std::ostringstream ss;
            ss << file.rdbuf();
            
            crow::response res;
            res.code = 200;
            res.body = ss.str();
            res.set_header("Content-Type", "application/octet-stream");

            // 3. 헤더 설정 (한글 파일명 처리)
            // PathToStr 헬퍼를 사용하여 ANSI 변환 에러(500 Crash)를 방지하고,
            // UTF-8 파일명을 그대로 헤더에 실어 보냅니다.
            std::string filename_utf8 = StorageHandler::PathToStr(StorageHandler::ToPath(full_path).filename());
            res.set_header("Content-Disposition", "attachment; filename=\"" + filename_utf8 + "\"");
            
            spdlog::info("[Req] File Download: {} ({} bytes)", full_path, res.body.size());
            return res;
        }
        return crow::response(404, "File not found");
    });

    // 서버 시작
    spdlog::info("Listening on port {}", cm.config.port);
    app.port(cm.config.port).multithreaded().run();
}