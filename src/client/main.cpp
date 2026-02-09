#include <iostream>
#include <string>
#include <fstream>
#include <filesystem>
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <windows.h> // [Essential] 콘솔 설정

// [Fix] u8path 경고 무시
#define _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING

using json = nlohmann::json;
const std::string AGENT_URL = "https://localhost";

// ---------------------------------------------------------
// Helpers
// ---------------------------------------------------------

std::string RemoveQuotes(std::string path) {
    if (path.size() >= 2 && path.front() == '"' && path.back() == '"') {
        return path.substr(1, path.size() - 2);
    }
    return path;
}

// [Input Helper] UTF-8 String -> Windows Path
std::filesystem::path ToPath(const std::string& utf8_str) {
    return std::filesystem::u8path(utf8_str);
}

// [Output Helper] Windows Path -> UTF-8 String
std::string PathToStr(const std::filesystem::path& p) {
    std::u8string u8 = p.u8string();
    return std::string(reinterpret_cast<const char*>(u8.c_str()));
}

// ---------------------------------------------------------
// Features
// ---------------------------------------------------------

void TestDownload() {
    std::string target;
    std::cout << "\n[Input] Enter NAS Path (e.g. PacsShort\\Study01\\Series001): ";
    
    std::cin.ignore(); 
    std::getline(std::cin, target);
    target = RemoveQuotes(target);

    json body;
    body["target_path"] = target;

    spdlog::info("Sending NAS Download Request: {}", target);
    
    auto r = cpr::Post(cpr::Url{AGENT_URL + "/pacs/download"},
                       cpr::Body{body.dump()},
                       cpr::Header{{"Content-Type", "application/json"}},
                       cpr::VerifySsl{false});

    spdlog::info("Status: {}", r.status_code);
    spdlog::info("Response: {}", r.text);
}

void TestDelete() {
    std::string target;
    std::cout << "\n[Input] Enter Folder Name to Delete (e.g. Series001): ";
    
    std::cin.ignore();
    std::getline(std::cin, target);
    target = RemoveQuotes(target);

    json body;
    body["target_path"] = target;

    spdlog::info("Sending Delete Request: {}", target);
    
    auto r = cpr::Post(cpr::Url{AGENT_URL + "/pacs/delete"},
                       cpr::Body{body.dump()},
                       cpr::Header{{"Content-Type", "application/json"}},
                       cpr::VerifySsl{false});

    spdlog::info("Status: {}", r.status_code);
    spdlog::info("Response: {}", r.text);
}

void TestFileUpload() {
    std::string file_path_str;
    std::cout << "\n[Input] Enter Local File Path to Upload: ";
    
    std::cin.ignore();
    std::getline(std::cin, file_path_str);
    file_path_str = RemoveQuotes(file_path_str);
    
    std::filesystem::path file_path = ToPath(file_path_str);

    if (!std::filesystem::exists(file_path)) {
        spdlog::error("[Error] File NOT found: {}", PathToStr(file_path));
        return;
    }

    std::ifstream f(file_path, std::ios::binary | std::ios::ate);
    if (f.good()) {
        auto size = f.tellg();
        spdlog::info("[Debug] File Size: {} bytes", static_cast<long long>(size));
        f.close();
    }

    std::string safe_path = PathToStr(file_path);
    spdlog::info("Uploading {}...", safe_path);

    cpr::Multipart multipart_data{};
    multipart_data.parts.push_back({"file", cpr::File(safe_path)});

    auto r = cpr::Post(cpr::Url{AGENT_URL + "/file/upload"},
                       multipart_data,
                       cpr::VerifySsl{false});

    spdlog::info("Status: {}", r.status_code);
    spdlog::info("Response: {}", r.text);
}

void TestFileDownload() {
    std::string target_path;
    std::cout << "\n[Input] Enter Relative Path (e.g. Uploads\\제목 없음.png): ";
    
    std::cin.ignore(); 
    std::getline(std::cin, target_path);
    target_path = RemoveQuotes(target_path);

    std::string user_input;
    std::cout << "[Input] Enter Save Path (Folder or File) [Default: Current Dir]: ";
    
    // getline 사용 (ignore 불필요)
    std::getline(std::cin, user_input);
    user_input = RemoveQuotes(user_input);

    std::filesystem::path final_save_path;
    
    if (user_input.empty()) {
        final_save_path = std::filesystem::path("Downloaded_") += ToPath(target_path).filename();
    } else {
        std::filesystem::path p = ToPath(user_input);
        if (std::filesystem::is_directory(p)) {
            final_save_path = p / ToPath(target_path).filename();
        } else {
            final_save_path = p;
        }
    }

    std::string final_path_str = PathToStr(final_save_path);
    spdlog::info("Downloading {} -> {} ...", target_path, final_path_str);

    auto r = cpr::Get(cpr::Url{AGENT_URL + "/file/download"},
                      cpr::Parameters{{"path", target_path}},
                      cpr::VerifySsl{false});

    if (r.status_code == 200) {
        std::ofstream out(final_save_path, std::ios::binary);
        if (out.is_open()) {
            out << r.text;
            out.close();
            spdlog::info("Success! Saved to: {}", final_path_str);
            spdlog::info("Size: {} bytes", r.text.size());
        } else {
            spdlog::error("Failed to open save path: {}", final_path_str);
        }
    } else {
        spdlog::error("Failed. Status: {}", r.status_code);
        spdlog::info("Response: {}", r.text);
    }
}

int main() {
    // [Crucial] 윈도우 콘솔 UTF-8 설정
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    spdlog::set_pattern("%^[%L] %v%$");
    
    while(true) {
        std::cout << "\n=== HIRA Helper Client v2.9.2 (Space Support) ===\n";
        std::cout << "1. PACS: Download Folder\n";
        std::cout << "2. PACS: Delete Folder\n";
        std::cout << "3. FILE: Upload File\n";
        std::cout << "4. FILE: Download File\n";
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