#include "crow.h"
#include "StorageHandler.hpp"
#include "ConfigManager.hpp"
#include "HtmlTemplates.hpp" 
#include <sstream>

// [Fix] URL Decoding Helper
// 브라우저는 공백을 '+'로, 특수문자를 '%XX'로 보냅니다. 이를 다시 원래대로 되돌립니다.
std::string UrlDecode(const std::string& value) {
    std::string result;
    result.reserve(value.length());
    for (size_t i = 0; i < value.length(); ++i) {
        if (value[i] == '+') {
            result += ' ';
        } else if (value[i] == '%' && i + 2 < value.length()) {
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
    ConfigManager cm;
    spdlog::set_level(spdlog::level::info);
    spdlog::info("=== HIRA Helper Agent v2.0 Started ===");

    // 캐시 루트 폴더 자동 생성
    std::filesystem::create_directories(cm.config.cache_root);

    crow::SimpleApp app;

    // [GET] /config - Admin UI
    CROW_ROUTE(app, "/config")([&]() {
        std::string html = CONFIG_HTML;
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
        return html;
    });

    // [POST] /config - 설정 저장 (수동 파싱 적용)
    CROW_ROUTE(app, "/config").methods(crow::HTTPMethod::POST)
    ([&](const crow::request& req) {
        // 1. Raw Body 확인
        spdlog::info("[Config] Raw Body Received: {}", req.body);

        // 2. Body Parsing (key=value&key2=value2)
        std::stringstream ss(req.body);
        std::string segment;
        
        while (std::getline(ss, segment, '&')) {
            size_t splitPos = segment.find('=');
            if (splitPos != std::string::npos) {
                std::string key = segment.substr(0, splitPos);
                std::string val = UrlDecode(segment.substr(splitPos + 1));

                // 값 매핑
                if (key == "nas_short_ip") cm.config.nas_short_ip = val;
                else if (key == "nas_long_ip") cm.config.nas_long_ip = val;
                else if (key == "port") {
                    try { cm.config.port = std::stoi(val); } catch(...) {}
                }
                else if (key == "cache_root") cm.config.cache_root = val;
            }
        }

        // 3. 저장 및 로그
        cm.Save();
        spdlog::info("[Config] Updated -> Short: {}, Root: {}", cm.config.nas_short_ip, cm.config.cache_root);

        return crow::response(200, "<h1>Saved!</h1><p>Please restart the service.</p><a href='/config'>Back</a>");
    });

    // [POST] /pacs/download
    CROW_ROUTE(app, "/pacs/download").methods(crow::HTTPMethod::POST)
    ([&](const crow::request& req) {
        auto x = crow::json::load(req.body);
        if (!x || !x.has("target_path")) 
            return crow::response(400, "Invalid JSON: 'target_path' required");

        std::string target = x["target_path"].s();
        
        // UNC 경로 조립 수정
        // 코드상 "\\\\" -> 실제값 "\\" (UNC 시작)
        // 코드상 "\\"   -> 실제값 "\" (구분자)
        std::string unc_path = "\\\\" + cm.config.nas_short_ip + "\\" + target;
        
        spdlog::info("[Req] Download from: {}", unc_path);
        
        spdlog::info("[Req] Download from: {}", unc_path);

        std::string local_path;
        if (StorageHandler::DownloadFolder(unc_path, cm.config.cache_root, local_path)) {
            crow::json::wvalue res;
            res["status"] = "success";
            res["local_path"] = local_path;
            return crow::response(200, res);
        }
        return crow::response(500, "Download Failed");
    });

    // [POST] /pacs/delete
    CROW_ROUTE(app, "/pacs/delete").methods(crow::HTTPMethod::POST)
    ([&](const crow::request& req) {
        auto x = crow::json::load(req.body);
        if (!x || !x.has("target_path")) 
            return crow::response(400, "Missing 'target_path'");
        
        std::string target_subpath = x["target_path"].s();
        fs::path full_path = fs::path(cm.config.cache_root) / target_subpath;

        if (StorageHandler::DeleteFolder(full_path.string())) {
            crow::json::wvalue res;
            res["status"] = "deleted";
            return crow::response(200, res);
        }
        return crow::response(404, "Folder not found");
    });

    spdlog::info("Listening on port {}", cm.config.port);
    spdlog::info("Admin UI: http://localhost:{}/config", cm.config.port);

    app.port(cm.config.port).multithreaded().run();
}