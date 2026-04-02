/**
 * @file main.cpp
 * @brief HIRA Helper Agent v3.1 (Strict 2-Folder Architecture & One-Shot Zip) 테스트용 C++ 클라이언트.
 *
 * 윈도우 콘솔에서 실행되며, cpr(C++ Requests) 라이브러리를 사용하여 
 * 서버와 HTTPS(REST API) 통신을 수행합니다.
 * * * [v3.1 변경사항 적용 내역]
 * - NAS 폴더 원샷 다운로드 테스트 (pacs/download): 폴더 통째로 다운 및 7-Zip 압축 테스트
 * - NAS ZIP 압축 해제 테스트 (pacs/extract) 신규 메뉴 추가
 * - URL 인코딩 일관성 패치 (모든 file_name 및 target_path 에 적용)
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

/**
 * @brief [Client Helper] 문자열을 URL 인코딩된 형태로 변환합니다.
 * 델파이의 TIdURI.ParamsEncode 와 동일한 역할을 수행합니다.
 */
std::string UrlEncode(const std::string& value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;

    for (char c : value) {
        // 영숫자와 일부 안전한 문자는 그대로 유지
        if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            // 나머지는 %XX 형태로 변환 (한글, 공백, + 기호 등 완벽 처리)
            escaped << std::uppercase;
            escaped << '%' << std::setw(2) << int((unsigned char)c);
            escaped << std::nouppercase;
        }
    }
    return escaped.str();
}

// =========================================================
// 기능 테스트 구현부 (Features)
// =========================================================

/**
 * @brief 1. [NAS -> Downloads] NAS 폴더 원샷 다운로드 & 압축 (v3.1)
 * 사용자가 지정한 NAS 원본 폴더 경로를 넘기면, 서버가 전체 복사 및 7-Zip 압축 후 ZIP 경로를 반환합니다.
 */
void TestNasDownload() {
    std::string target;
    std::cout << "\n[Input] Enter NAS Folder Path (e.g. PacsShort\\2024\\Study01): ";
    
    std::cin.ignore(); 
    std::getline(std::cin, target);
    target = RemoveQuotes(target);

    // JSON 본문 구성 (URL 인코딩 처리)
    json body;
    body["target_path"] = UrlEncode(target);

    spdlog::info("Sending NAS Folder One-Shot Download Request: {}", target);
    spdlog::info("Waiting for server to copy and zip... (This may take a while)");
    
    // POST 요청 전송 (서버 사설 인증서 무시를 위해 VerifySsl{false} 옵션 적용)
    auto r = cpr::Post(cpr::Url{AGENT_URL + "/pacs/download"},
                       cpr::Body{body.dump()},
                       cpr::Header{{"Content-Type", "application/json"}},
                       cpr::VerifySsl{false});

    // 결과 출력 (성공 시 result: true 와 생성된 zip_path 가 리턴됨)
    spdlog::info("Status: {}", r.status_code);
    spdlog::info("Response: {}", r.text);
}

/**
 * @brief 2. [Downloads 폴더] 단일 파일 삭제
 * HIRA API 첨부파일 또는 NAS에서 받아온 파일을 서버의 Downloads 폴더에서 삭제합니다.
 */
void TestDeleteDownloadsFile() {
    std::string filename;
    std::cout << "\n[Input] Enter File Name in 'Downloads' Folder (e.g. 20260401123456.zip): ";
    
    std::cin.ignore();
    std::getline(std::cin, filename);
    filename = RemoveQuotes(filename);

    json body;
    body["file_name"] = UrlEncode(filename);

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
    std::string encoded_filename = UrlEncode(PathToStr(file_path.filename()));
    
    cpr::Multipart multipart_data{};
    // cpr::File{실제파일경로, 서버에알려줄파일명} 형태로 오버로딩 호출
    multipart_data.parts.push_back({"file", cpr::File{safe_path, encoded_filename}});

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
 * @brief 5. [Uploads 폴더] 단일 파일 삭제
 * 서버의 Uploads 폴더에 업로드 해두었던 파일을 사용 후 삭제할 때 호출합니다.
 */
void TestDeleteUploadsFile() {
    std::string filename;
    std::cout << "\n[Input] Enter File Name in 'Uploads' Folder (e.g. ScanData.jpg): ";
    
    std::cin.ignore();
    std::getline(std::cin, filename);
    filename = RemoveQuotes(filename);

    json body;
    // [버그 패치] 여기도 일관성 확보를 위해 UrlEncode 적용
    body["file_name"] = UrlEncode(filename);

    spdlog::info("Sending Delete Request for Uploads: {}", filename);
    
    auto r = cpr::Post(cpr::Url{AGENT_URL + "/file/delete"},
                       cpr::Body{body.dump()},
                       cpr::Header{{"Content-Type", "application/json"}},
                       cpr::VerifySsl{false});

    spdlog::info("Status: {}", r.status_code);
    spdlog::info("Response: {}", r.text);
}

/**
 * @brief 6. [Downloads] ZIP 파일 7-Zip 압축 해제 (신규)
 * 서버의 Downloads 폴더에 존재하는 ZIP 파일명을 보내면, 서버가 이를 초고속 해제하고 DCM 메타데이터를 반환합니다.
 */
void TestPacsExtract() {
    std::string filename;
    std::cout << "\n[Input] Enter ZIP File Name in 'Downloads' Folder (e.g. 20260401123456.zip): ";
    
    std::cin.ignore();
    std::getline(std::cin, filename);
    filename = RemoveQuotes(filename);

    json body;
    body["file_name"] = UrlEncode(filename);

    spdlog::info("Sending 7-Zip Extract Request for: {}", filename);
    spdlog::info("Waiting for extraction to complete...");
    
    auto r = cpr::Post(cpr::Url{AGENT_URL + "/pacs/extract"},
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
        std::cout << "\n=== HIRA Helper Client v3.1 (One-Shot Zip Mode) ===\n";
        std::cout << "1. NAS Folder -> Server Zip (pacs/download)\n";
        std::cout << "2. Delete from Server (Downloads)\n";
        std::cout << "3. Client -> Server (Uploads)\n";
        std::cout << "4. Server -> Client (Downloads)\n";
        std::cout << "5. Delete from Server (Uploads)\n";
        std::cout << "6. Extract ZIP in Server (pacs/extract) [NEW]\n";
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
        if (choice == 5) TestDeleteUploadsFile(); 
        if (choice == 6) TestPacsExtract();       // 신규 메뉴 연결
    }
    return 0;
}