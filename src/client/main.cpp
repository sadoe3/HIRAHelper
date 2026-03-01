/**
 * @file main.cpp
 * @brief HIRA Helper Agent v3.0 (Strict 2-Folder Architecture) 테스트용 C++ 클라이언트.
 *
 * 윈도우 콘솔에서 실행되며, cpr(C++ Requests) 라이브러리를 사용하여 
 * 서버와 HTTPS(REST API) 통신을 수행합니다.
 * * [v3.0 변경사항 적용 내역]
 * - 폴더 단위 제어에서 단일 파일(File Name) 제어로 전면 개편
 * - NAS 다운로드(pacs/download) -> 결과물은 무조건 서버의 Downloads 폴더로 들어감
 * - 파일 업로드(file/upload) -> 결과물은 무조건 서버의 Uploads 폴더로 들어감
 * - 파일 다운로드(file/download) -> 무조건 서버의 Downloads 폴더 안에서 찾아서 가져옴
 * - [신규] Uploads 폴더 전용 삭제 기능 추가
 */

#include <iostream>
#include <string>
#include <fstream>
#include <filesystem>
#include <cpr/cpr.h>        // HTTP 통신을 위한 외부 라이브러리
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <windows.h>        // 콘솔에 UTF-8 출력을 강제하기 위한 윈도우 API

// MSVC 환경에서 C++20 u8path 사용 시 발생하는 경고를 무시합니다.
#define _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING

using json = nlohmann::json;

// 에이전트 서버 주소 (로컬 IIS 리버스 프록시를 통과하므로 HTTPS 사용)
const std::string AGENT_URL = "https://localhost";

// =========================================================
// 공통 유틸리티 (Helpers)
// =========================================================

/**
 * @brief 콘솔에 파일 경로를 드래그 앤 드롭할 때 생기는 앞뒤 따옴표(")를 제거합니다.
 */
std::string RemoveQuotes(std::string path) {
    if (path.size() >= 2 && path.front() == '"' && path.back() == '"') {
        return path.substr(1, path.size() - 2);
    }
    return path;
}

/**
 * @brief UTF-8 문자열을 윈도우 네이티브 파일 시스템 경로 객체로 변환합니다.
 */
std::filesystem::path ToPath(const std::string& utf8_str) {
    return std::filesystem::u8path(utf8_str);
}

/**
 * @brief 윈도우 경로 객체를 안전한 UTF-8 문자열로 변환합니다. (로깅/통신용)
 */
std::string PathToStr(const std::filesystem::path& p) {
    std::u8string u8 = p.u8string();
    return std::string(reinterpret_cast<const char*>(u8.c_str()));
}

// =========================================================
// 기능 테스트 구현부 (Features)
// =========================================================

/**
 * @brief 1. [NAS -> Downloads] 단일 파일 복사 요청
 * 사용자가 지정한 NAS 원본 경로에서 파일 하나를 찾아 서버의 Downloads 폴더로 복사합니다.
 */
void TestNasDownload() {
    std::string target;
    std::cout << "\n[Input] Enter NAS File Path (e.g. PacsShort\\Study01\\Image.dcm): ";
    
    std::cin.ignore(); 
    std::getline(std::cin, target);
    target = RemoveQuotes(target);

    // JSON 본문 구성 (v3.0에서도 NAS 경로는 target_path 파라미터 사용)
    json body;
    body["target_path"] = target;

    spdlog::info("Sending NAS Download Request: {}", target);
    
    // POST 요청 전송 (서버 사설 인증서 무시를 위해 VerifySsl{false} 옵션 적용)
    auto r = cpr::Post(cpr::Url{AGENT_URL + "/pacs/download"},
                       cpr::Body{body.dump()},
                       cpr::Header{{"Content-Type", "application/json"}},
                       cpr::VerifySsl{false});

    // 결과 출력 (성공 시 200과 함께 다운로드된 실제 경로 리턴됨)
    spdlog::info("Status: {}", r.status_code);
    spdlog::info("Response: {}", r.text);
}

/**
 * @brief 2. [Downloads 폴더] 단일 파일 삭제
 * HIRA API 첨부파일 또는 NAS에서 받아온 파일을 서버의 Downloads 폴더에서 삭제합니다.
 */
void TestDeleteDownloadsFile() {
    std::string filename;
    std::cout << "\n[Input] Enter File Name in 'Downloads' Folder (e.g. Image.dcm): ";
    
    std::cin.ignore();
    std::getline(std::cin, filename);
    filename = RemoveQuotes(filename);

    // [v3.0 변경] 파라미터명이 target_path에서 file_name으로 변경됨
    json body;
    body["file_name"] = filename;

    spdlog::info("Sending Delete Request for Downloads: {}", filename);
    
    auto r = cpr::Post(cpr::Url{AGENT_URL + "/pacs/delete"},
                       cpr::Body{body.dump()},
                       cpr::Header{{"Content-Type", "application/json"}},
                       cpr::VerifySsl{false});

    spdlog::info("Status: {}", r.status_code);
    spdlog::info("Response: {}", r.text);
}

/**
 * @brief 3. [Local -> Uploads] 클라이언트 파일 업로드
 * 내 PC의 파일을 읽어서 서버의 Uploads 폴더로 전송합니다.
 */
void TestFileUpload() {
    std::string file_path_str;
    std::cout << "\n[Input] Enter Local File Path to Upload: ";
    
    std::cin.ignore();
    std::getline(std::cin, file_path_str);
    file_path_str = RemoveQuotes(file_path_str);
    
    // 한글 경로 처리를 위해 UTF-8 경로 객체로 변환
    std::filesystem::path file_path = ToPath(file_path_str);

    if (!std::filesystem::exists(file_path)) {
        spdlog::error("[Error] File NOT found: {}", PathToStr(file_path));
        return;
    }

    std::string safe_path = PathToStr(file_path);
    spdlog::info("Uploading {} to server 'Uploads' folder...", safe_path);

    // cpr Multipart 양식 구성. 내부적으로 파일을 읽어서 스트리밍합니다.
    cpr::Multipart multipart_data{};
    multipart_data.parts.push_back({"file", cpr::File(safe_path)});

    auto r = cpr::Post(cpr::Url{AGENT_URL + "/file/upload"},
                       multipart_data,
                       cpr::VerifySsl{false});

    spdlog::info("Status: {}", r.status_code);
    spdlog::info("Response: {}", r.text);
}

/**
 * @brief 4. [Downloads -> Local] 서버 파일 다운로드
 * 서버의 Downloads 폴더에 있는 파일을 클라이언트 PC로 스트림 다운로드합니다.
 */
void TestFileDownload() {
    std::string target_filename;
    std::cout << "\n[Input] Enter File Name in Server 'Downloads' folder (e.g. Doc.pdf): ";
    
    std::cin.ignore(); 
    std::getline(std::cin, target_filename);
    target_filename = RemoveQuotes(target_filename);

    std::string user_input;
    std::cout << "[Input] Enter Save Path (Folder or File) [Default: Current Dir]: ";
    std::getline(std::cin, user_input);
    user_input = RemoveQuotes(user_input);

    std::filesystem::path final_save_path;
    
    // 로컬 저장 경로 파싱 로직
    if (user_input.empty()) {
        final_save_path = std::filesystem::path("Downloaded_") += ToPath(target_filename).filename();
    } else {
        std::filesystem::path p = ToPath(user_input);
        if (std::filesystem::is_directory(p)) {
            final_save_path = p / ToPath(target_filename).filename();
        } else {
            final_save_path = p;
        }
    }

    std::string final_path_str = PathToStr(final_save_path);
    spdlog::info("Downloading {} -> {} ...", target_filename, final_path_str);

    // [v3.0 변경] Query 파라미터 이름이 path에서 file_name으로 변경됨
    auto r = cpr::Get(cpr::Url{AGENT_URL + "/file/download"},
                      cpr::Parameters{{"file_name", target_filename}},
                      cpr::VerifySsl{false});

    if (r.status_code == 200) {
        // 성공 시 받아온 바이너리(r.text)를 로컬 디스크에 저장
        std::ofstream out(final_save_path, std::ios::binary);
        if (out.is_open()) {
            out << r.text;
            out.close();
            spdlog::info("Success! Saved to: {}", final_path_str);
            spdlog::info("Size: {} bytes", r.text.size());
        } else {
            spdlog::error("Failed to open local save path: {}", final_path_str);
        }
    } else {
        spdlog::error("Failed. Status: {}", r.status_code);
        spdlog::info("Response Error Message: {}", r.text);
    }
}

/**
 * @brief 5. [Uploads 폴더] 단일 파일 삭제 (신규)
 * 서버의 Uploads 폴더에 업로드 해두었던 파일을 사용 후 삭제할 때 호출합니다.
 */
void TestDeleteUploadsFile() {
    std::string filename;
    std::cout << "\n[Input] Enter File Name in 'Uploads' Folder (e.g. ScanData.jpg): ";
    
    std::cin.ignore();
    std::getline(std::cin, filename);
    filename = RemoveQuotes(filename);

    json body;
    body["file_name"] = filename;

    spdlog::info("Sending Delete Request for Uploads: {}", filename);
    
    // 신규 라우팅 경로 /file/delete 호출
    auto r = cpr::Post(cpr::Url{AGENT_URL + "/file/delete"},
                       cpr::Body{body.dump()},
                       cpr::Header{{"Content-Type", "application/json"}},
                       cpr::VerifySsl{false});

    spdlog::info("Status: {}", r.status_code);
    spdlog::info("Response: {}", r.text);
}

// =========================================================
// 메인 루프 (Main Loop)
// =========================================================

int main() {
    // 콘솔 창의 입력 및 출력 코드페이지를 UTF-8(65001)로 강제 설정하여 한글 깨짐을 완벽 방지합니다.
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    // Spdlog 출력 형식 지정 (시간 생략, 직관적인 메시지 중심)
    spdlog::set_pattern("%^[%L] %v%$");
    
    while(true) {
        std::cout << "\n=== HIRA Helper Client v3.0 (Strict File Mode) ===\n";
        std::cout << "1. NAS -> Server (Downloads)\n";
        std::cout << "2. Delete from Server (Downloads)\n";
        std::cout << "3. Client -> Server (Uploads)\n";
        std::cout << "4. Server -> Client (Downloads)\n";
        std::cout << "5. Delete from Server (Uploads)\n";
        std::cout << "0. Exit\n";
        std::cout << "Select: ";
        
        int choice;
        // 숫자가 아닌 값(문자 등) 입력 시 예외 처리
        if (!(std::cin >> choice)) {
            std::cin.clear(); 
            std::cin.ignore(1000, '\n'); 
            continue;
        }

        if (choice == 0) break;
        if (choice == 1) TestNasDownload();
        if (choice == 2) TestDeleteDownloadsFile();
        if (choice == 3) TestFileUpload();
        if (choice == 4) TestFileDownload();
        if (choice == 5) TestDeleteUploadsFile(); // 신규 메뉴 연결
    }
    return 0;
}