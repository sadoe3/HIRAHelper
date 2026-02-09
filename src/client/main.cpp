#include <iostream>
#include <string>
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

using json = nlohmann::json;
const std::string AGENT_URL = "http://localhost:8080";

void TestDownload() {
    std::string target;
    std::cout << "\n[Input] Enter NAS Path (e.g. PacsShort\\Study01\\Series001): ";
    std::cin >> target;

    json body;
    body["target_path"] = target;

    spdlog::info("Sending Download Request...");
    auto r = cpr::Post(cpr::Url{AGENT_URL + "/pacs/download"},
                       cpr::Body{body.dump()},
                       cpr::Header{{"Content-Type", "application/json"}});

    spdlog::info("Status: {}", r.status_code);
    spdlog::info("Response: {}", r.text);
}

void TestDelete() {
    std::string target;
    std::cout << "\n[Input] Enter Local Folder Name to Delete (e.g. Series001): ";
    std::cin >> target;

    json body;
    body["target_path"] = target;

    spdlog::info("Sending Delete Request...");
    auto r = cpr::Post(cpr::Url{AGENT_URL + "/pacs/delete"},
                       cpr::Body{body.dump()},
                       cpr::Header{{"Content-Type", "application/json"}});

    spdlog::info("Status: {}", r.status_code);
    spdlog::info("Response: {}", r.text);
}

int main() {
    spdlog::set_pattern("%^[%L] %v%$"); // 로그 포맷 간소화
    
    while(true) {
        std::cout << "\n=== HIRA Test Client ===\n";
        std::cout << "1. Download Folder (NAS -> Local)\n";
        std::cout << "2. Delete Folder (Local)\n";
        std::cout << "0. Exit\n";
        std::cout << "Select: ";
        
        int choice;
        if (!(std::cin >> choice)) {
            std::cin.clear(); std::cin.ignore(1000, '\n'); continue;
        }

        if (choice == 0) break;
        if (choice == 1) TestDownload();
        if (choice == 2) TestDelete();
    }
    return 0;
}