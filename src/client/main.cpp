#include <iostream>
#include <string>
#include <fstream>
#include <filesystem>
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

using json = nlohmann::json;

// Phase 3 적용: HTTPS (IIS Proxy)
const std::string AGENT_URL = "https://localhost";

// [Helper] 윈도우 경로 따옴표 제거
std::string RemoveQuotes(std::string path) {
    if (path.size() >= 2 && path.front() == '"' && path.back() == '"') {
        return path.substr(1, path.size() - 2);
    }
    return path;
}

// 1. NAS 폴더 다운로드
void TestDownload() {
    std::string target;
    // Strict Routing 적용: 반드시 PacsShort 또는 PacsLong으로 시작해야 함
    std::cout << "\n[Input] Enter NAS Path (e.g. PacsShort\\Study01\\Series001): ";
    std::cin.ignore(); 
    std::getline(std::cin, target);
    target = RemoveQuotes(target);

    json body;
    body["target_path"] = target;

    spdlog::info("Sending NAS Download Request...");
    auto r = cpr::Post(cpr::Url{AGENT_URL + "/pacs/download"},
                       cpr::Body{body.dump()},
                       cpr::Header{{"Content-Type", "application/json"}},
                       cpr::VerifySsl{false});

    spdlog::info("Status: {}", r.status_code);
    spdlog::info("Response: {}", r.text);
}

// 2. 로컬 폴더 삭제
void TestDelete() {
    std::string target;
    std::cout << "\n[Input] Enter Folder Name to Delete (e.g. Series001): ";
    std::cin >> target; 

    json body;
    body["target_path"] = target;

    spdlog::info("Sending Delete Request...");
    auto r = cpr::Post(cpr::Url{AGENT_URL + "/pacs/delete"},
                       cpr::Body{body.dump()},
                       cpr::Header{{"Content-Type", "application/json"}},
                       cpr::VerifySsl{false});

    spdlog::info("Status: {}", r.status_code);
    spdlog::info("Response: {}", r.text);
}

// 3. 파일 업로드
void TestFileUpload() {
    std::string file_path;
    std::cout << "\n[Input] Enter Local File Path to Upload: ";
    
    std::cin.ignore();
    std::getline(std::cin, file_path);
    file_path = RemoveQuotes(file_path);

    if (!std::filesystem::exists(file_path)) {
        spdlog::error("[Debug] File NOT found: {}", file_path);
        return;
    }

    std::ifstream f(file_path, std::ios::binary | std::ios::ate);
    if (f.good()) {
        auto size = f.tellg();
        spdlog::info("[Debug] File Size: {} bytes", static_cast<long long>(size));
        f.close();
    }

    spdlog::info("Uploading {}...", file_path);

    cpr::Multipart multipart_data{};
    multipart_data.parts.push_back({"file", cpr::File(file_path)});

    auto r = cpr::Post(cpr::Url{AGENT_URL + "/file/upload"},
                       multipart_data,
                       cpr::VerifySsl{false});

    spdlog::info("Status: {}", r.status_code);
    spdlog::info("Response: {}", r.text);
}

// 4. 파일 다운로드 (디렉토리 지원)
void TestFileDownload() {
    std::string target_path;
    std::cout << "\n[Input] Enter Relative Path (e.g. Uploads\\test.jpg): ";
    std::cin >> target_path; 

    // 저장 경로 입력받기
    std::string user_input;
    std::cout << "[Input] Enter Save Path (Folder or File) [Default: Current Dir]: ";
    std::cin.ignore();
    std::getline(std::cin, user_input);
    user_input = RemoveQuotes(user_input);

    std::string final_save_path;
    
    if (user_input.empty()) {
        final_save_path = "Downloaded_" + std::filesystem::path(target_path).filename().string();
    } else {
        std::filesystem::path p(user_input);
        if (std::filesystem::is_directory(p)) {
            // 폴더 입력 시 파일명 자동 추가
            final_save_path = (p / std::filesystem::path(target_path).filename()).string();
        } else {
            // 파일명 입력 시 그대로 사용
            final_save_path = user_input;
        }
    }

    spdlog::info("Downloading {} -> {} ...", target_path, final_save_path);

    auto r = cpr::Get(cpr::Url{AGENT_URL + "/file/download"},
                      cpr::Parameters{{"path", target_path}},
                      cpr::VerifySsl{false});

    if (r.status_code == 200) {
        std::ofstream out(final_save_path, std::ios::binary);
        if (out.is_open()) {
            out << r.text;
            out.close();
            spdlog::info("Success! Saved to: {}", final_save_path);
            spdlog::info("Size: {} bytes", r.text.size());
        } else {
            spdlog::error("Failed to open save path: {}", final_save_path);
        }
    } else {
        spdlog::error("Failed. Status: {}", r.status_code);
        spdlog::info("Response: {}", r.text);
    }
}

int main() {
    spdlog::set_pattern("%^[%L] %v%$");
    
    while(true) {
        std::cout << "\n=== HIRA Helper Client v2.5 (Final) ===\n";
        std::cout << "1. PACS: Download Folder (NAS -> Local)\n";
        std::cout << "2. PACS: Delete Folder\n";
        std::cout << "3. FILE: Upload File (Local -> Server)\n";
        std::cout << "4. FILE: Download File (Server -> Local)\n";
        std::cout << "0. Exit\n";
        std::cout << "Select: ";
        
        int choice;
        if (!(std::cin >> choice)) {
            std::cin.clear(); 
            std::cin.ignore(1000, '\n'); 
            continue;
        }

        if (choice == 0) break;
        if (choice == 1) TestDownload();
        if (choice == 2) TestDelete();
        if (choice == 3) TestFileUpload();
        if (choice == 4) TestFileDownload();
    }
    return 0;
}