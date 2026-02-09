#include "crow.h"
#include "StorageHandler.hpp"
#include "ConfigManager.hpp"
#include "HtmlTemplates.hpp"
#include "Logger.hpp"
#include "Cleaner.hpp"
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
    Logger::Init();
    ConfigManager cm;
    spdlog::info("=== HIRA Helper Agent v2.9.2 (Robust Server) Started ===");

    Cleaner::Start(cm);
    std::filesystem::create_directories(cm.config.cache_root);

    crow::SimpleApp app;

    // [Config Route]
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
        replace(html, "{{RETENTION}}", std::to_string(cm.config.retention_days));
        replace(html, "{{INTERVAL}}", std::to_string(cm.config.cleaner_interval_days));
        replace(html, "{{CLEANER_CHECKED}}", cm.config.cleaner_enabled ? "checked" : "");
        return html;
    });

    CROW_ROUTE(app, "/config").methods(crow::HTTPMethod::POST)
    ([&](const crow::request& req) {
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
    // [PACS Route]
    // =========================================================
    CROW_ROUTE(app, "/pacs/download").methods(crow::HTTPMethod::POST)
    ([&](const crow::request& req) {
        auto x = crow::json::load(req.body);
        if (!x || !x.has("target_path")) return crow::response(400, "Invalid JSON");
        
        std::string target = x["target_path"].s();
        std::string target_ip;
        std::string relative_path;

        if (target.rfind("PacsShort", 0) == 0) { 
            target_ip = cm.config.nas_short_ip;
            if (target.length() > 9) relative_path = target.substr(10);
            else relative_path = ""; 
        } 
        else if (target.rfind("PacsLong", 0) == 0) {
            target_ip = cm.config.nas_long_ip;
            if (target.length() > 8) relative_path = target.substr(9);
            else relative_path = "";
        }
        else {
            spdlog::warn("[Req] Blocked Invalid Prefix: {}", target);
            return crow::response(400, "Invalid Path Prefix");
        }

        std::string unc_path = "\\\\" + target_ip + "\\" + relative_path;
        
        spdlog::info("[Req] NAS Download: {} -> UNC: {}", target, unc_path);
        
        std::string local_path;
        if (StorageHandler::DownloadFolder(unc_path, cm.config.cache_root, local_path)) {
            crow::json::wvalue res;
            res["status"] = "success";
            res["local_path"] = local_path;
            return crow::response(200, res);
        }
        return crow::response(500, "Download Failed");
    });

    CROW_ROUTE(app, "/pacs/delete").methods(crow::HTTPMethod::POST)
    ([&](const crow::request& req) {
        auto x = crow::json::load(req.body);
        if (!x || !x.has("target_path")) return crow::response(400, "Missing 'target_path'");
        std::string target = x["target_path"].s();
        fs::path full_path = fs::path(cm.config.cache_root) / target; 
        if (StorageHandler::DeleteFolder(full_path.string())) { 
            crow::json::wvalue res;
            res["status"] = "deleted";
            return crow::response(200, res);
        }
        return crow::response(404, "Folder not found");
    });

    // =========================================================
    // [File Route] Upload
    // =========================================================
    CROW_ROUTE(app, "/file/upload").methods(crow::HTTPMethod::POST)
    ([&](const crow::request& req) {
        crow::multipart::message msg(req);
        int saved_count = 0;
        std::string upload_dir = (fs::path(cm.config.cache_root) / "Uploads").string();

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
                        
                        spdlog::info("[Req] File Upload: {}", raw_name);
                        
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
    // [File Route] Download (Stream + Safe UTF8)
    // =========================================================
    CROW_ROUTE(app, "/file/download").methods(crow::HTTPMethod::GET)
    ([&](const crow::request& req) {
        auto path_param = req.url_params.get("path");
        if (!path_param) return crow::response(400, "Missing 'path' parameter");

        std::string full_path;
        // 1. 파일 경로 확인 (full_path는 PathToStr에 의해 안전한 UTF-8 문자열로 반환됨)
        if (StorageHandler::GetFileForDownload(cm.config.cache_root, path_param, full_path)) {
            
            // 2. 파일 열기: ToPath로 다시 path 객체로 변환하여 엶
            std::ifstream file(StorageHandler::ToPath(full_path), std::ios::binary);
            if (!file.is_open()) return crow::response(500, "File open error");

            std::ostringstream ss;
            ss << file.rdbuf();
            
            crow::response res;
            res.code = 200;
            res.body = ss.str();
            res.set_header("Content-Type", "application/octet-stream");

            // 3. 헤더 설정: PathToStr를 사용하여 안전하게 UTF-8 문자열 추출 (500 Error 방지)
            std::string filename_utf8 = StorageHandler::PathToStr(StorageHandler::ToPath(full_path).filename());
            res.set_header("Content-Disposition", "attachment; filename=\"" + filename_utf8 + "\"");
            
            spdlog::info("[Req] File Download: {} ({} bytes)", full_path, res.body.size());
            return res;
        }
        return crow::response(404, "File not found");
    });

    spdlog::info("Listening on port {}", cm.config.port);
    app.port(cm.config.port).multithreaded().run();
}