#include <iostream>
#include <string>
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

using json = nlohmann::json;

// [수정 1] HTTPS 프로토콜 사용 (포트 443은 생략 가능)
const std::string AGENT_URL = "https://localhost";

void TestDownload() {
    std::string target;
    std::cout << "\n[Input] Enter NAS Path (e.g. PacsShort\\Study01\\Series001): ";
    std::cin >> target;

    json body;
    body["target_path"] = target;

    spdlog::info("Sending Download Request...");
    
    // [수정 2] VerifySsl{false} 옵션 추가 (사설 인증서 에러 무시)
    auto r = cpr::Post(cpr::Url{AGENT_URL + "/pacs/download"},
                       cpr::Body{body.dump()},
                       cpr::Header{{"Content-Type", "application/json"}},
                       cpr::VerifySsl{false}); 

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
    
    // [수정 3] VerifySsl{false} 옵션 추가
    auto r = cpr::Post(cpr::Url{AGENT_URL + "/pacs/delete"},
                       cpr::Body{body.dump()},
                       cpr::Header{{"Content-Type", "application/json"}},
                       cpr::VerifySsl{false}); 

    spdlog::info("Status: {}", r.status_code);
    spdlog::info("Response: {}", r.text);
}

int main() {
    spdlog::set_pattern("%^[%L] %v%$");
    
    while(true) {
        std::cout << "\n=== HIRA Test Client (HTTPS) ===\n";
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