#pragma once
#include <fstream>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <filesystem>

using json = nlohmann::json;

struct AppConfig {
    std::string nas_short_ip = "127.0.0.1";
    std::string nas_long_ip = "127.0.0.1";
    int port = 8080;
    std::string cache_root = "C:\\Temp\\HiraCache";
    
    // [Phase 4] Disk Management Configs
    bool cleaner_enabled = true;
    int cleaner_interval_days = 1;
    int retention_days = 30;
};

class ConfigManager {
public:
    AppConfig config;
    const std::string file = "config.json";

    ConfigManager() { Load(); }

    void Load() {
        if (std::filesystem::exists(file)) {
            try {
                std::ifstream i(file); json j; i >> j;
                
                config.nas_short_ip = j.value("nas_short_ip", config.nas_short_ip);
                config.nas_long_ip = j.value("nas_long_ip", config.nas_long_ip);
                config.port = j.value("port", config.port);
                config.cache_root = j.value("cache_root", config.cache_root);
                
                // [Phase 4] 신규 설정 로드
                config.cleaner_enabled = j.value("cleaner_enabled", true);
                config.cleaner_interval_days = j.value("cleaner_interval_days", 1);
                config.retention_days = j.value("retention_days", 30);
                
            } catch (...) { spdlog::error("Config Load Failed, using defaults"); }
        } else { Save(); }
    }

    void Save() {
        json j;
        j["nas_short_ip"] = config.nas_short_ip;
        j["nas_long_ip"] = config.nas_long_ip;
        j["port"] = config.port;
        j["cache_root"] = config.cache_root;
        j["cleaner_enabled"] = config.cleaner_enabled;
        j["cleaner_interval_days"] = config.cleaner_interval_days;
        j["retention_days"] = config.retention_days;
        std::ofstream o(file); o << j.dump(4);
    }
};