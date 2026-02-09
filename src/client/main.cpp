/**
 * @file main.cpp
 * @brief HIRA Helper Agent와 통신하는 테스트용 클라이언트 프로그램.
 * * * 윈도우 콘솔에서 실행되며, 사용자의 입력을 받아 HTTPS 요청을 보냅니다.
 * * 주요 기능:
 * 1. PACS 폴더 다운로드 요청 (NAS -> Local Cache)
 * 2. PACS 폴더 삭제 요청
 * 3. 로컬 파일 업로드 (Local -> Server Cache)
 * 4. 서버 파일 다운로드 (Server Cache -> Local)
 * * * [UTF-8 Support] 윈도우 콘솔 입출력 및 파일 경로 처리에 UTF-8을 사용하여
 * 한글 파일명 깨짐을 방지합니다.
 */

#include <iostream>
#include <string>
#include <fstream>
#include <filesystem>
#include <cpr/cpr.h>        // HTTP 통신 라이브러리
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <windows.h>        // 콘솔 코드페이지(CP_UTF8) 설정을 위해 필요

// [Fix] C++20 u8path 경고 무시 (MSVC 호환성)
#define _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING

using json = nlohmann::json;

// 서버 주소 (IIS Reverse Proxy를 통하므로 HTTPS 사용)
const std::string AGENT_URL = "https://localhost";

// ---------------------------------------------------------
// Helpers
// ---------------------------------------------------------

/**
 * @brief 윈도우 경로 복사 시 생기는 따옴표(") 제거 함수
 * 예: "C:\Path\To\File" -> C:\Path\To\File
 */
std::string RemoveQuotes(std::string path) {
    if (path.size() >= 2 && path.front() == '"' && path.back() == '"') {
        return path.substr(1, path.size() - 2);
    }
    return path;
}

/**
 * @brief [Input Helper] UTF-8 String -> Windows Path 변환
 * 클라이언트 내부에서 파일 시스템 접근 시 사용
 */
std::filesystem::path ToPath(const std::string& utf8_str) {
    return std::filesystem::u8path(utf8_str);
}

/**
 * @brief [Output Helper] Windows Path -> UTF-8 String 변환
 * 로그 출력 및 서버 전송 시 사용
 */
std::string PathToStr(const std::filesystem::path& p) {
    std::u8string u8 = p.u8string();
    return std::string(reinterpret_cast<const char*>(u8.c_str()));
}

// ---------------------------------------------------------
// Features
// ---------------------------------------------------------

/**
 * @brief 1. PACS 폴더 다운로드 요청
 * 사용자가 입력한 NAS 경로(예: PacsShort\Study01)를 서버에 전송하여 다운로드를 지시합니다.
 */
void TestDownload() {
    std::string target;
    std::cout << "\n[Input] Enter NAS Path (e.g. PacsShort\\Study01\\Series001): ";
    
    // 입력 버퍼 비우기 (이전 메뉴 선택 시 남은 엔터 제거)
    std::cin.ignore(); 
    // 공백 포함 경로 입력을 위해 getline 사용
    std::getline(std::cin, target);
    target = RemoveQuotes(target);

    // JSON Body 생성
    json body;
    body["target_path"] = target;

    spdlog::info("Sending NAS Download Request: {}", target);
    
    // HTTPS POST 요청 (SSL 검증 비활성화: 사설 인증서 사용 시 필수)
    auto r = cpr::Post(cpr::Url{AGENT_URL + "/pacs/download"},
                       cpr::Body{body.dump()},
                       cpr::Header{{"Content-Type", "application/json"}},
                       cpr::VerifySsl{false});

    spdlog::info("Status: {}", r.status_code);
    spdlog::info("Response: {}", r.text);
}

/**
 * @brief 2. PACS 폴더 삭제 요청
 * 서버 캐시 폴더 내의 특정 폴더 삭제를 요청합니다.
 */
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

/**
 * @brief 3. 로컬 파일 업로드 (Multipart)
 * 로컬 파일을 읽어서 서버의 Uploads 폴더로 전송합니다.
 * 한글 파일명도 깨짐 없이 전송되도록 처리합니다.
 */
void TestFileUpload() {
    std::string file_path_str;
    std::cout << "\n[Input] Enter Local File Path to Upload: ";
    
    std::cin.ignore();
    std::getline(std::cin, file_path_str);
    file_path_str = RemoveQuotes(file_path_str);
    
    // UTF-8 경로 객체 생성
    std::filesystem::path file_path = ToPath(file_path_str);

    // 파일 존재 여부 확인 (ToPath 덕분에 한글 경로도 인식 가능)
    if (!std::filesystem::exists(file_path)) {
        spdlog::error("[Error] File NOT found: {}", PathToStr(file_path));
        return;
    }

    // 파일 크기 확인 (디버깅용)
    std::ifstream f(file_path, std::ios::binary | std::ios::ate);
    if (f.good()) {
        auto size = f.tellg();
        spdlog::info("[Debug] File Size: {} bytes", static_cast<long long>(size));
        f.close();
    }

    // 전송을 위해 UTF-8 문자열로 변환
    std::string safe_path = PathToStr(file_path);
    spdlog::info("Uploading {}...", safe_path);

    // CPR Multipart 생성 (파일 경로 전달 시 cpr 내부적으로 파일을 엽니다)
    cpr::Multipart multipart_data{};
    multipart_data.parts.push_back({"file", cpr::File(safe_path)});

    auto r = cpr::Post(cpr::Url{AGENT_URL + "/file/upload"},
                       multipart_data,
                       cpr::VerifySsl{false});

    spdlog::info("Status: {}", r.status_code);
    spdlog::info("Response: {}", r.text);
}

/**
 * @brief 4. 서버 파일 다운로드 (Stream)
 * 서버에 있는 파일을 스트림으로 받아 로컬에 저장합니다.
 * 한글 파일명 및 띄어쓰기가 포함된 경로를 지원합니다.
 */
void TestFileDownload() {
    std::string target_path;
    std::cout << "\n[Input] Enter Relative Path (e.g. Uploads\\제목 없음.png): ";
    
    std::cin.ignore(); 
    std::getline(std::cin, target_path);
    target_path = RemoveQuotes(target_path);

    std::string user_input;
    std::cout << "[Input] Enter Save Path (Folder or File) [Default: Current Dir]: ";
    
    // getline 사용 (앞선 ignore 덕분에 여기선 바로 입력받음)
    std::getline(std::cin, user_input);
    user_input = RemoveQuotes(user_input);

    std::filesystem::path final_save_path;
    
    // 저장 경로 결정 로직
    if (user_input.empty()) {
        // 입력 없으면: 현재 경로 + Downloaded_ + 원본파일명
        final_save_path = std::filesystem::path("Downloaded_") += ToPath(target_path).filename();
    } else {
        std::filesystem::path p = ToPath(user_input);
        if (std::filesystem::is_directory(p)) {
            // 폴더 입력 시: 해당 폴더 + 원본파일명
            final_save_path = p / ToPath(target_path).filename();
        } else {
            // 파일명까지 입력 시: 해당 경로 사용
            final_save_path = p;
        }
    }

    std::string final_path_str = PathToStr(final_save_path);
    spdlog::info("Downloading {} -> {} ...", target_path, final_path_str);

    // GET 요청 (Query Parameter로 경로 전달)
    auto r = cpr::Get(cpr::Url{AGENT_URL + "/file/download"},
                      cpr::Parameters{{"path", target_path}},
                      cpr::VerifySsl{false});

    if (r.status_code == 200) {
        // 받은 데이터를 로컬 파일에 저장 (Binary 모드)
        // [Crucial] ToPath를 사용하지 않고 저장하면 한글 윈도우가 아닐 때 파일명이 깨짐
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
    // [Crucial] 윈도우 콘솔 입출력을 UTF-8(Code Page 65001)로 강제 설정
    // 이것이 없으면 한글 입력 시 깨진 문자열(Mojibake)이 전달됩니다.
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