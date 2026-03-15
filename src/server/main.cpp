/**
 * @file main.cpp
 * @brief HIRA Helper Agent 서버의 메인 진입점.
 *
 * Crow 프레임워크를 기반으로 동작하는 RESTful API 서버입니다.
 * 이 서버는 로컬 환경과 NAS(PACS) 간의 파일 송수신을 중계하며, 
 * 보안 및 관리의 편의성을 위해 파일 저장소를 2개의 전용 폴더(Downloads, Uploads)로 
 * 엄격하게 분리하여 운영합니다 (Strict 2-Folder Architecture).
 */

#include "crow.h"
#include "crow\logging.h"
#include "StorageHandler.hpp"
#include "ConfigManager.hpp"
#include "HtmlTemplates.hpp"
#include "Logger.hpp"
#include "Cleaner.hpp"
#include <sstream>

/**
 * @brief URL 인코딩된 문자열을 일반 문자열로 디코딩합니다.
 *
 * 웹 폼(Form) 전송 시 공백이 '+'로 바뀌거나, 특수문자가 '%XX' 형태로 
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

/**
 * @brief Crow의 자체 로그를 가로채서 우리의 spdlog 파일에 기록하는 커스텀 핸들러
 *
 * 불필요한 HTTP INFO 접속 로그는 무시하고, Warning 이상의 중요한 내부 에러만
 * 일별 백그라운드 로그 파일(agent_YYYY-MM-DD.log)에 통합하여 기록합니다.
 */
class CrowToSpdlogLogger : public crow::ILogHandler {
public:
    void log(const std::string& message, crow::LogLevel level) {
        std::string messageToLog(message);
        // 메시지 끝에 줄바꿈이 있으면 제거 (spdlog가 알아서 줄바꿈을 해주므로)
        if (!messageToLog.empty() && messageToLog.back() == '\n') {
            messageToLog.pop_back();
        }

        // Crow의 로그 레벨에 따라 spdlog로 토스
        // Debug와 Info 레벨은 무시함 (service_out.log 용량 폭주 방지)
        switch (level) {
            case crow::LogLevel::Warning:
                spdlog::warn("[Crow] {}", messageToLog);
                break;
            case crow::LogLevel::Error:
                spdlog::error("[Crow] {}", messageToLog);
                break;
            case crow::LogLevel::Critical:
                spdlog::critical("[Crow] {}", messageToLog);
                break;
        }
    }
};

int main() {
    // 1. 비동기 로깅 시스템 초기화 (일별 파일 출력 전용)
    Logger::Init();
    
    // 2. Crow 프레임워크의 콘솔 출력을 억제하고 spdlog로 핸들링
    static CrowToSpdlogLogger custom_logger;
    crow::logger::setHandler(&custom_logger);

    // 3. 설정 파일(config.json) 로드
    ConfigManager cm;
    spdlog::info("=== HIRA Helper Started ===");

    // 4. 백그라운드 자동 파일 정리(Cleaner) 데몬 스레드 시작
    Cleaner::Start(cm);

    // 5. 전용 샌드박스 캐시 폴더 할당 및 강제 생성
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
        
        // HTML 내의 치환 태그({{TAG}})를 실제 설정값으로 매핑
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
    // 웹 폼에서 전송된 설정 데이터를 파싱하여 config.json에 물리적으로 기록
    CROW_ROUTE(app, "/config").methods(crow::HTTPMethod::POST)([&](const crow::request& req) {
        bool new_cleaner_enabled = false;
        std::stringstream ss(req.body);
        std::string segment;
        
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
        cm.Save(); 
        
        return crow::response(200, "<h1>Saved!</h1><p>Restart required.</p><a href='/config'>Back</a>");
    });

    // =========================================================
    // [Route] NAS 단일 파일 다운로드 (POST /pacs/download)
    // =========================================================
    // NAS UNC 경로에서 지정된 파일을 서버의 Downloads 폴더로 고속 복사합니다.
    CROW_ROUTE(app, "/pacs/download").methods(crow::HTTPMethod::POST)
    ([&](const crow::request& req) {
        auto x = crow::json::load(req.body);
        if (!x || !x.has("target_path")) return crow::response(400, "Invalid JSON");
        
        std::string target = UrlDecode(x["target_path"].s()); 
        std::string target_ip, relative_path;

        std::string sub_dir = "";
        if (x.has("sub_dir")) {
            sub_dir = UrlDecode(x["sub_dir"].s());
        }
        spdlog::info("[API] Requested PACS download: {} (Sub-dir: {})", target, sub_dir.empty() ? "None" : sub_dir);

        // 보안 접두사(Prefix) 라우팅 검증
        if (target.rfind("PacsShort", 0) == 0) { 
            target_ip = cm.config.nas_short_ip;
            relative_path = target.length() > 9 ? target.substr(10) : ""; 
        } 
        else if (target.rfind("PacsLong", 0) == 0) {
            target_ip = cm.config.nas_long_ip;
            relative_path = target.length() > 8 ? target.substr(9) : "";
        }
        else {
            spdlog::error("[API] PACS download failed (Invalid Prefix): {}", target);
            return crow::response(400, "Invalid Path Prefix");
        }

        std::string unc_path = "\\\\" + target_ip + "\\" + relative_path;

        fs::path dest_dir = fs::path(downloads_dir);
        if (!sub_dir.empty()) {
            dest_dir = dest_dir / StorageHandler::ToPath(StorageHandler::PathToStr(fs::path(sub_dir).filename()));
            if (!fs::exists(dest_dir)) {
                fs::create_directories(dest_dir); 
            }
        }

        std::string local_path;
        if (StorageHandler::DownloadSingleFile(unc_path, dest_dir.string(), local_path)) {
            spdlog::info("[API] PACS download SUCCESS -> Saved at: {}", local_path);
            crow::json::wvalue res;
            res["status"] = "success";
            res["local_path"] = local_path;
            return crow::response(200, res);
        }

        spdlog::error("[API] PACS download FAILED from: {}", unc_path);
        return crow::response(500, "Download Failed");
    });

    // =========================================================
    // [Route] Downloads 폴더 단일 파일 삭제 (POST /pacs/delete)
    // =========================================================
    CROW_ROUTE(app, "/pacs/delete").methods(crow::HTTPMethod::POST)
    ([&](const crow::request& req) {
        auto x = crow::json::load(req.body);
        if (!x || !x.has("file_name")) return crow::response(400, "Missing 'file_name'");
        
        std::string file_name = UrlDecode(x["file_name"].s());
        spdlog::info("[API] Requested deletion in Downloads: {}", file_name);
        
        // 경로 탐색 공격(Directory Traversal) 차단
        if (!StorageHandler::IsValidFileName(file_name)) return crow::response(400, "Invalid file name");

        fs::path full_path = fs::path(downloads_dir) / StorageHandler::ToPath(file_name);

        if (StorageHandler::DeleteSingleFile(StorageHandler::PathToStr(full_path))) { 
            spdlog::info("[API] Deletion SUCCESS: {}", file_name);
            crow::json::wvalue res;
            res["status"] = "deleted";
            return crow::response(200, res);
        }

        spdlog::warn("[API] Deletion FAILED (File not found): {}", file_name);
        return crow::response(404, "File not found");
    });

    // =========================================================
    // [Route] Downloads 폴더 내 ZIP 파일 압축 해제 (POST /pacs/extract)
    // =========================================================
    // 지정된 ZIP 파일의 압축을 해제합니다. 
    // 압축은 별도의 서브 폴더를 생성하지 않고 ZIP 파일이 위치한 현재 폴더에 그대로 풀립니다.
    // 해제 완료 후, 해당 폴더 내의 '.dcm' 확장자 파일들의 메타데이터를 반환합니다.
    CROW_ROUTE(app, "/pacs/extract").methods(crow::HTTPMethod::POST)
    ([&](const crow::request& req) {
        auto x = crow::json::load(req.body);
        if (!x || !x.has("file_name")) return crow::response(400, "Missing 'file_name'");
        
        std::string file_name = UrlDecode(x["file_name"].s());
        spdlog::info("[API] Requested zip extraction: {}", file_name);

        if (file_name.find("..") != std::string::npos) {
            spdlog::warn("[API] Extraction blocked (Directory Traversal attempt): {}", file_name);
            return crow::response(400, "Invalid path structure");
        }

        fs::path zip_path = fs::path(downloads_dir) / StorageHandler::ToPath(file_name);

        if (!fs::exists(zip_path) || !fs::is_regular_file(zip_path)) {
            spdlog::warn("[API] Extraction FAILED (File not found): {}", file_name);
            return crow::response(404, "File not found");
        }

        std::string ext = zip_path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext != ".zip") {
            spdlog::warn("[API] Extraction FAILED (Not a zip file): {}", file_name);
            return crow::response(400, "Target is not a .zip file");
        }

        // 별도의 서브 폴더를 만들지 않고, ZIP 파일이 존재하는 '부모 폴더'를 대상 경로로 지정
        fs::path extract_dir = zip_path.parent_path();

        // 윈도우 내장 tar.exe를 호출하여 압축 해제
        if (StorageHandler::ExtractZip(zip_path, extract_dir)) {
            spdlog::info("[API] Extraction SUCCESS -> Extracted directly to: {}", StorageHandler::PathToStr(extract_dir));
            
            int dcm_files = 0;
            uintmax_t dcm_size_bytes = 0;

            // 압축이 풀린 폴더를 재귀적으로 스캔하여 '.dcm' 파일의 통계만 집계
            try {
                for (const auto& entry : fs::recursive_directory_iterator(extract_dir)) {
                    if (fs::is_regular_file(entry.status())) {
                        std::string entry_ext = entry.path().extension().string();
                        std::transform(entry_ext.begin(), entry_ext.end(), entry_ext.begin(), ::tolower);
                        
                        // C-STORE 전송을 위해 DCM 파일만 카운트
                        if (entry_ext == ".dcm") {
                            dcm_files++;
                            dcm_size_bytes += fs::file_size(entry);
                        }
                    }
                }
                spdlog::info("[API] DCM content stats: {} files, {} bytes", dcm_files, dcm_size_bytes);
            } catch (const fs::filesystem_error& e) {
                spdlog::warn("[API] Failed to calculate extracted size/count: {}", e.what());
            }

            // 클라이언트 처리를 용이하게 하기 위해 Bytes를 MB로 변환 (소수점 둘째 자리 반올림)
            double total_size_mb = std::round((static_cast<double>(dcm_size_bytes) / (1024.0 * 1024.0)) * 100.0) / 100.0;

            crow::json::wvalue res;
            res["status"] = "success";
            res["extract_path"] = StorageHandler::PathToStr(extract_dir);  
            res["total_dcm_files"] = dcm_files;
            res["total_dcm_size_mb"] = total_size_mb; 
            
            return crow::response(200, res);
        }

        spdlog::error("[API] Extraction FAILED for: {}", file_name);
        return crow::response(500, "Extraction Failed");
    });

    // =========================================================
    // [Route] 전송 세션별 고유 임시 폴더 생성 (POST /pacs/mkdir)
    // =========================================================
    // 클라이언트가 다수의 파일과 ZIP을 모아서 처리하기 전,
    // 현재 시간(초 단위: YYYYMMDDHHMMSS)을 기반으로 고유 샌드박스 폴더를 할당받습니다.
    CROW_ROUTE(app, "/pacs/mkdir").methods(crow::HTTPMethod::POST)
    ([&](const crow::request& /*req*/) {
        auto now = std::time(nullptr);
        auto tm = *std::localtime(&now);
        std::ostringstream ss;
        ss << std::put_time(&tm, "%Y%m%d%H%M%S"); // 예: 20260315143045
        std::string unique_folder_name = ss.str();

        fs::path new_dir = fs::path(downloads_dir) / StorageHandler::ToPath(unique_folder_name);

        // 하드디스크 용량 부족이나 권한 에러를 대비한 방어 로직
        try {
            if (!fs::exists(new_dir)) {
                fs::create_directories(new_dir);
            }
        } catch (const fs::filesystem_error& e) {
            spdlog::error("[API] Failed to create session folder: {}", e.what());
            return crow::response(500, "Directory Creation Failed");
        }

        spdlog::info("[API] Created unique session folder: {}", unique_folder_name);

        crow::json::wvalue res;
        res["status"] = "success";
        res["folder_name"] = unique_folder_name;               
        res["full_path"] = StorageHandler::PathToStr(new_dir); 
        return crow::response(200, res);
    });

    // =========================================================
    // [Route] 클라이언트 파일 업로드 (POST /file/upload)
    // =========================================================
    // 클라이언트가 보낸 Multipart Form Data를 안전한 원자적 쓰기(Atomic Write)로 Uploads 폴더에 저장합니다.
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
                        
                        if (raw_name.size() >= 2 && raw_name.front() == '"' && raw_name.back() == '"') {
                            raw_name = raw_name.substr(1, raw_name.size() - 2);
                        }
                        
                        raw_name = UrlDecode(raw_name);
                        
                        if (StorageHandler::SaveFileAtomic(uploads_dir, raw_name, part.body)) {
                            spdlog::info("[API] File upload SUCCESS: {}", raw_name);
                            saved_count++;
                        }
                        else 
                            spdlog::error("[API] File upload FAILED: {}", raw_name);
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
    // 서버의 Downloads 폴더 내 특정 파일을 클라이언트 측으로 바이너리 스트리밍합니다.
    CROW_ROUTE(app, "/file/download").methods(crow::HTTPMethod::GET)
    ([&](const crow::request& req) {
        auto name_param = req.url_params.get("file_name"); 
        if (!name_param) return crow::response(400, "Missing 'file_name' parameter");
        
        std::string file_name = UrlDecode(std::string(name_param));
        spdlog::info("[API] Requested file download: {}", file_name);

        if (!StorageHandler::IsValidFileName(file_name)) return crow::response(400, "Invalid file name");

        fs::path full_path = fs::path(downloads_dir) / StorageHandler::ToPath(file_name);

        if (!fs::exists(full_path) || !fs::is_regular_file(full_path)) {
            spdlog::warn("[API] File download FAILED (Not found): {}", file_name);
            return crow::response(404, "File not found");
        }

        std::ifstream file(full_path, std::ios::binary);
        if (!file.is_open()) {
            spdlog::error("[API] File download FAILED (Open error): {}", file_name);
            return crow::response(500, "File open error");
        }

        std::ostringstream ss;
        ss << file.rdbuf();
        
        crow::response res;
        res.code = 200;
        res.body = ss.str();
        res.set_header("Content-Type", "application/octet-stream");
        
        std::string filename_utf8 = StorageHandler::PathToStr(full_path.filename());
        res.set_header("Content-Disposition", "attachment; filename=\"" + filename_utf8 + "\"");
        
        spdlog::info("[API] File download SUCCESS: {}", file_name);
        return res;
    });

    // =========================================================
    // [Route] Uploads 폴더 단일 파일 삭제 (POST /file/delete)
    // =========================================================
    CROW_ROUTE(app, "/file/delete").methods(crow::HTTPMethod::POST)
    ([&](const crow::request& req) {
        auto x = crow::json::load(req.body);
        if (!x || !x.has("file_name")) return crow::response(400, "Missing 'file_name'");
        
        std::string file_name = UrlDecode(x["file_name"].s());
        spdlog::info("[API] Requested deletion in Uploads: {}", file_name);

        if (!StorageHandler::IsValidFileName(file_name)) return crow::response(400, "Invalid file name");

        fs::path full_path = fs::path(uploads_dir) / StorageHandler::ToPath(file_name);

        if (StorageHandler::DeleteSingleFile(StorageHandler::PathToStr(full_path))) { 
            spdlog::info("[API] Deletion SUCCESS: {}", file_name);
            crow::json::wvalue res;
            res["status"] = "deleted";
            return crow::response(200, res);
        }

        spdlog::warn("[API] Deletion FAILED (File not found): {}", file_name);
        return crow::response(404, "File not found");
    });

    // 서버 시작
    spdlog::info("Listening on port {}", cm.config.port);
    app.port(cm.config.port).multithreaded().run();
}