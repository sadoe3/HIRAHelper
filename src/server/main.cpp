#include "crow.h"
#include "StorageHandler.hpp"
#include "ConfigManager.hpp"
#include "HtmlTemplates.hpp"
#include "Logger.hpp"   // [New]
#include "Cleaner.hpp"  // [New]
#include <sstream>

// URL Decoding Helper
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
    // 1. 로거 초기화 (가장 먼저)
    Logger::Init();
    
    ConfigManager cm;
    spdlog::info("=== HIRA Helper Agent v2.1 (Phase 4) Started ===");

    // 2. 클리너 스레드 시작
    Cleaner::Start(cm);

    // 3. 캐시 폴더 생성
    std::filesystem::create_directories(cm.config.cache_root);

    crow::SimpleApp app;

    // [GET] /config - UI 렌더링
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
        
        // [New] Cleaner 설정 매핑
        replace(html, "{{RETENTION}}", std::to_string(cm.config.retention_days));
        replace(html, "{{INTERVAL}}", std::to_string(cm.config.cleaner_interval_days));
        replace(html, "{{CLEANER_CHECKED}}", cm.config.cleaner_enabled ? "checked" : "");

        return html;
    });

    // [POST] /config - 설정 저장 (Manual Parsing)
    CROW_ROUTE(app, "/config").methods(crow::HTTPMethod::POST)
    ([&](const crow::request& req) {
        spdlog::info("[Config] Raw Body: {}", req.body);

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
                else if (key == "cleaner_enabled") {
                    new_cleaner_enabled = true; 
                }
            }
        }
        
        cm.config.cleaner_enabled = new_cleaner_enabled;
        cm.Save();
        spdlog::info("[Config] Saved. Cleaner: {}", cm.config.cleaner_enabled ? "ON" : "OFF");

        return crow::response(200, "<h1>Saved!</h1><p>Restart required.</p><a href='/config'>Back</a>");
    });

    // [POST] /pacs/download
    CROW_ROUTE(app, "/pacs/download").methods(crow::HTTPMethod::POST)
    ([&](const crow::request& req) {
        auto x = crow::json::load(req.body);
        if (!x || !x.has("target_path")) 
            return crow::response(400, "Invalid JSON: 'target_path' required");

        std::string target = x["target_path"].s();
        
        // [Fixed] UNC Path Construction
        // 코드상 "\\" -> 실제 "\"
        std::string unc_path = "\\\\" + cm.config.nas_short_ip + "\\" + target;
        
        spdlog::info("[Req] Download: {}", unc_path);

        std::string local_path;
        if (StorageHandler::DownloadFolder(unc_path, cm.config.cache_root, local_path)) {
            crow::json::wvalue res;
            res["status"] = "success";
            res["local_path"] = local_path;
            spdlog::info("[IO] Download Success");
            return crow::response(200, res);
        }
        spdlog::error("[IO] Download Failed");
        return crow::response(500, "Download Failed");
    });

    // [POST] /pacs/delete
    CROW_ROUTE(app, "/pacs/delete").methods(crow::HTTPMethod::POST)
    ([&](const crow::request& req) {
        auto x = crow::json::load(req.body);
        if (!x || !x.has("target_path")) return crow::response(400, "Missing 'target_path'");
        
        std::string target = x["target_path"].s();
        fs::path full_path = fs::path(cm.config.cache_root) / target;

        if (StorageHandler::DeleteFolder(full_path.string())) {
            crow::json::wvalue res;
            res["status"] = "deleted";
            spdlog::info("[IO] Delete Success: {}", target);
            return crow::response(200, res);
        }
        return crow::response(404, "Folder not found");
    });

    spdlog::info("Listening on port {}", cm.config.port);
    app.port(cm.config.port).multithreaded().run();
}