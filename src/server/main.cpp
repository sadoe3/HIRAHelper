/**
 * @file main.cpp
 * @brief HIRA Helper Agent 서버의 메인 진입점.
 * * Crow 프레임워크를 기반으로 동작하는 RESTful API 서버입니다.
 * 이 서버는 로컬 환경과 NAS(PACS) 간의 파일 송수신을 중계하며, 
 * 보안 및 관리의 편의성을 위해 파일 저장소를 2개의 전용 폴더(Downloads, Uploads)로 
 * 엄격하게 분리하여 운영합니다 (Strict 2-Folder Architecture).
 */

#include "crow.h"
#include "StorageHandler.hpp"
#include "ConfigManager.hpp"
#include "HtmlTemplates.hpp"
#include "Logger.hpp"
#include "Cleaner.hpp"
#include <sstream>

/**
 * @brief URL 인코딩된 문자열을 일반 문자열로 디코딩합니다.
 * * 웹 폼(Form) 전송 시 공백이 '+'로 바뀌거나, 특수문자가 '%XX' 형태로 
 * 인코딩되는 것을 원래의 문자(UTF-8)로 복원합니다.
 * * @param value URL 인코딩된 원본 문자열
 * @return std::string 디코딩된 일반 문자열
 */
std::string UrlDecode(const std::string& value) {
    std::string result;
    result.reserve(value.length());
    for (size_t i = 0; i < value.length(); ++i) {
        if (value[i] == '+') {
            result += ' '; // '+' 기호는 공백으로 변환
        } else if (value[i] == '%' && i + 2 < value.length()) {
            // '%' 뒤의 2자리 문자를 16진수(Hex) 코드로 해석하여 문자로 변환
            std::string hex = value.substr(i + 1, 2);
            char chr = (char)std::strtol(hex.c_str(), nullptr, 16);
            result += chr;
            i += 2;
        } else {
            result += value[i];
        }
    }
    return result;
}

int main() {
    // 1. 비동기 로깅 시스템 초기화 (콘솔 및 파일 출력)
    Logger::Init();
    
    // 2. 설정 파일(config.json) 로드
    ConfigManager cm;
    spdlog::info("=== HIRA Helper Agent Started ===");

    // 3. 백그라운드 자동 파일 정리(Cleaner) 스레드 시작
    Cleaner::Start(cm);

    // 4. 전용 캐시 폴더 할당 및 강제 생성
    // 서버가 관리하는 모든 파일은 이 두 폴더 내에만 존재하도록 격리합니다.
    std::string downloads_dir = (fs::path(cm.config.cache_root) / "Downloads").string();
    std::string uploads_dir = (fs::path(cm.config.cache_root) / "Uploads").string();
    fs::create_directories(downloads_dir);
    fs::create_directories(uploads_dir);

    crow::SimpleApp app;

    // =========================================================
    // [Route] 설정 페이지 뷰 (GET /config)
    // =========================================================
    // 관리자가 브라우저를 통해 현재 서버 설정을 조회할 수 있는 HTML 렌더링
    CROW_ROUTE(app, "/config")([&]() {
        std::string html = CONFIG_HTML;
        
        // HTML 내의 치환 태그({{TAG}})를 실제 설정값으로 매핑하는 람다 함수
        auto replace = [&](std::string& str, const std::string& key, const std::string& val) {
            size_t pos = 0;
            while ((pos = str.find(key, pos)) != std::string::npos) {
                str.replace(pos, key.length(), val);
                pos += val.length();
            }
        };
        
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
    // [Route] 설정 저장 (POST /config)
    // =========================================================
    // 웹 폼에서 전송된 application/x-www-form-urlencoded 데이터를 파싱하여 config.json에 저장
    CROW_ROUTE(app, "/config").methods(crow::HTTPMethod::POST)([&](const crow::request& req) {
        bool new_cleaner_enabled = false;
        std::stringstream ss(req.body);
        std::string segment;
        
        // '&' 기준으로 파라미터 분리 후 키-값 파싱
        while (std::getline(ss, segment, '&')) {
            size_t splitPos = segment.find('=');
            if (splitPos != std::string::npos) {
                std::string key = segment.substr(0, splitPos);
                std::string val = UrlDecode(segment.substr(splitPos + 1));
                
                if (key == "nas_short_ip") cm.config.nas_short_ip = val;
                else if (key == "nas_long_ip") cm.config.nas_long_ip = val;
                else if (key == "port") try { cm.config.port = std::stoi(val); } catch(...) {}
                else if (key == "cache_root") cm.config.cache_root = val;
                else if (key == "retention_days") try { cm.config.retention_days = std::stoi(val); } catch(...) {}
                else if (key == "cleaner_interval_days") try { cm.config.cleaner_interval_days = std::stoi(val); } catch(...) {}
                else if (key == "cleaner_enabled") new_cleaner_enabled = true; 
            }
        }
        
        cm.config.cleaner_enabled = new_cleaner_enabled;
        cm.Save(); // 변경된 설정을 물리 파일에 기록
        
        return crow::response(200, "<h1>Saved!</h1><p>Restart required.</p><a href='/config'>Back</a>");
    });

    // =========================================================
    // [Route] NAS 단일 파일 다운로드 (POST /pacs/download)
    // =========================================================
    // NAS에서 지정된 단일 .dcm 파일을 서버의 Downloads 폴더로 복사합니다.
    CROW_ROUTE(app, "/pacs/download").methods(crow::HTTPMethod::POST)
    ([&](const crow::request& req) {
        auto x = crow::json::load(req.body);
        if (!x || !x.has("target_path")) return crow::response(400, "Invalid JSON");
        
        // url 디코딩 처리를 통해 한국어 관련 문제 해결
        std::string target = UrlDecode(x["target_path"].s()); // 클라이언트가 요청한 NAS 내부 경로
        std::string target_ip, relative_path;

        // 접두사를 통해 내부 라우팅 보안 처리 (허용되지 않은 경로는 접근 원천 차단)
        if (target.rfind("PacsShort", 0) == 0) { 
            target_ip = cm.config.nas_short_ip;
            relative_path = target.length() > 9 ? target.substr(10) : ""; 
        } 
        else if (target.rfind("PacsLong", 0) == 0) {
            target_ip = cm.config.nas_long_ip;
            relative_path = target.length() > 8 ? target.substr(9) : "";
        }
        else return crow::response(400, "Invalid Path Prefix");

        // NAS UNC 경로 조립 (예: \\192.168.0.1\Study01\Image.dcm)
        std::string unc_path = "\\\\" + target_ip + "\\" + relative_path;
        std::string local_path;
        
        // 다운로드 실행: 결과물은 항상 Downloads 폴더로 들어감
        if (StorageHandler::DownloadSingleFile(unc_path, downloads_dir, local_path)) {
            crow::json::wvalue res;
            res["status"] = "success";
            res["local_path"] = local_path;
            return crow::response(200, res);
        }
        return crow::response(500, "Download Failed");
    });

    // =========================================================
    // [Route] Downloads 폴더 단일 파일 삭제 (POST /pacs/delete)
    // =========================================================
    // HIRA API 첨부파일 및 NAS 다운로드 파일 등 Downloads 폴더에 있는 파일을 삭제합니다.
    CROW_ROUTE(app, "/pacs/delete").methods(crow::HTTPMethod::POST)
    ([&](const crow::request& req) {
        auto x = crow::json::load(req.body);
        if (!x || !x.has("file_name")) return crow::response(400, "Missing 'file_name'");
        
        // url 디코딩 처리를 통해 한국어 관련 문제 해결
        std::string file_name = UrlDecode(x["file_name"].s());
        
        // 보안 검증: 경로 탐색(Path Traversal) 시도를 원천 차단
        if (!StorageHandler::IsValidFileName(file_name)) return crow::response(400, "Invalid file name");

        // 대상 파일의 절대 경로 조합
        fs::path full_path = fs::path(downloads_dir) / file_name;

        if (StorageHandler::DeleteSingleFile(full_path.string())) { 
            crow::json::wvalue res;
            res["status"] = "deleted";
            return crow::response(200, res);
        }
        return crow::response(404, "File not found");
    });

    // =========================================================
    // [Route] 클라이언트 파일 업로드 (POST /file/upload)
    // =========================================================
    // 클라이언트가 보낸 Multipart Form Data를 파싱하여 Uploads 폴더에 저장합니다.
    CROW_ROUTE(app, "/file/upload").methods(crow::HTTPMethod::POST)
    ([&](const crow::request& req) {
        crow::multipart::message msg(req);
        int saved_count = 0;

        for (const auto& part : msg.parts) {
            if (!part.body.empty()) {
                auto it = part.headers.find("Content-Disposition");
                if (it != part.headers.end()) {
                    auto& params = it->second.params;
                    auto filename_it = params.find("filename");
                    
                    if (filename_it != params.end()) {
                        std::string raw_name = filename_it->second;
                        
                        // 따옴표 제거
                        if (raw_name.size() >= 2 && raw_name.front() == '"' && raw_name.back() == '"') {
                            raw_name = raw_name.substr(1, raw_name.size() - 2);
                        }
                        
                        // 클라이언트가 URL 인코딩해서 보낸 파일명을 다시 UTF-8 한글로 복원
                        raw_name = UrlDecode(raw_name);
                        
                        // 이제 raw_name은 완벽한 "한글.jpg"가 되어 StorageHandler로 넘어갑니다.
                        if (StorageHandler::SaveFileAtomic(uploads_dir, raw_name, part.body)) {
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
    // [Route] 클라이언트 파일 다운로드 (GET /file/download)
    // =========================================================
    // Downloads 폴더 내에 존재하는 파일을 읽어 클라이언트에게 스트림 형태로 반환합니다.
    CROW_ROUTE(app, "/file/download").methods(crow::HTTPMethod::GET)
    ([&](const crow::request& req) {
        auto name_param = req.url_params.get("file_name"); 
        if (!name_param) return crow::response(400, "Missing 'file_name' parameter");
        
        // url 디코딩 처리를 통해 한국어 관련 문제 해결
        std::string file_name = UrlDecode(std::string(name_param));

        // 보안 검증: 경로 탐색(Path Traversal) 시도를 원천 차단
        if (!StorageHandler::IsValidFileName(file_name)) return crow::response(400, "Invalid file name");

        // 대상 파일의 절대 경로 조합 (무조건 Downloads 폴더 안에서만 탐색)
        fs::path full_path = fs::path(downloads_dir) / StorageHandler::ToPath(file_name);

        if (!fs::exists(full_path) || !fs::is_regular_file(full_path)) {
            return crow::response(404, "File not found");
        }

        // 바이너리 읽기 모드로 파일 오픈
        std::ifstream file(full_path, std::ios::binary);
        if (!file.is_open()) return crow::response(500, "File open error");

        // 파일 데이터를 메모리 버퍼에 담음
        std::ostringstream ss;
        ss << file.rdbuf();
        
        crow::response res;
        res.code = 200;
        res.body = ss.str();
        res.set_header("Content-Type", "application/octet-stream");
        
        // 한글 파일명이 깨지지 않도록 UTF-8 포맷으로 헤더에 명시
        std::string filename_utf8 = StorageHandler::PathToStr(full_path.filename());
        res.set_header("Content-Disposition", "attachment; filename=\"" + filename_utf8 + "\"");
        
        return res;
    });

    // =========================================================
    // [Route] Uploads 폴더 단일 파일 삭제 (POST /file/delete)
    // =========================================================
    // 업로드된 파일을 개별적으로 삭제할 때 사용하는 엔드포인트입니다.
    CROW_ROUTE(app, "/file/delete").methods(crow::HTTPMethod::POST)
    ([&](const crow::request& req) {
        auto x = crow::json::load(req.body);
        if (!x || !x.has("file_name")) return crow::response(400, "Missing 'file_name'");
        
        // url 디코딩 처리를 통해 한국어 관련 문제 해결
        std::string file_name = UrlDecode(x["file_name"].s());
        
        // 보안 검증: 경로 탐색(Path Traversal) 시도를 원천 차단
        if (!StorageHandler::IsValidFileName(file_name)) return crow::response(400, "Invalid file name");

        // 삭제 대상의 절대 경로 조합 (무조건 Uploads 폴더 안에서만 탐색)
        fs::path full_path = fs::path(uploads_dir) / file_name;

        if (StorageHandler::DeleteSingleFile(full_path.string())) { 
            crow::json::wvalue res;
            res["status"] = "deleted";
            return crow::response(200, res);
        }
        return crow::response(404, "File not found");
    });

    spdlog::info("Listening on port {}", cm.config.port);
    app.port(cm.config.port).multithreaded().run();
}