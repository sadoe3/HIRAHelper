#pragma once
#include <fstream>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <filesystem>

using json = nlohmann::json;

struct AppConfig {
    std::string nas_short_ip = "127.0.0.1"; // 기본값: 로컬 테스트용
    std::string nas_long_ip = "127.0.0.1";
    int port = 8080;
    std::string cache_root = "C:\\Temp\\HiraCache";
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
                spdlog::info("Config Loaded. Port: {}", config.port);
            } catch (...) { spdlog::error("Config Load Failed, using defaults"); }
        } else { Save(); }
    }

    void Save() {
        json j;
        j["nas_short_ip"] = config.nas_short_ip;
        j["nas_long_ip"] = config.nas_long_ip;
        j["port"] = config.port;
        j["cache_root"] = config.cache_root;
        std::ofstream o(file); o << j.dump(4);
    }
};