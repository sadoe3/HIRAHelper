/**
 * @file StorageHandler.hpp
 * @brief 파일 시스템(I/O) 작업을 처리하는 정적 유틸리티 클래스.
 *
 * NAS 폴더 다운로드(복사), 로컬 파일 삭제, Atomic 파일 업로드(임시파일 -> 이름변경),
 * 파일 경로 검증 등의 기능을 제공합니다.
 * 특히 UTF-8 문자열과 Windows 파일 경로(fs::path) 간의 안전한 변환을 지원하여
 * 다국어(한글 등) 파일명 처리를 보장합니다.
 */

#pragma once
#include <filesystem>
#include <string>
#include <fstream>
#include <spdlog/spdlog.h>

// [Fix] C++20 u8path 경고 무시 (MSVC 호환성)
// C++20부터 u8path가 deprecated 되었으나, Windows 환경에서 UTF-8 경로 처리를 위해
// 여전히 가장 확실한 방법이므로 경고를 끄고 사용합니다.
#define _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING

namespace fs = std::filesystem;

/**
 * @class StorageHandler
 * @brief 파일 입출력 및 경로 처리를 담당하는 클래스
 */
class StorageHandler {
public: 
    // main.cpp 등 외부에서도 경로 변환 헬퍼를 사용할 수 있도록 public으로 선언

    /**
     * @brief [Helper] UTF-8 String -> Windows Path 변환
     * * 클라이언트로부터 받은 UTF-8 문자열을 Windows OS가 이해하는
     * 유니코드(Wide String) 기반의 fs::path 객체로 변환합니다.
     * 이 함수를 거치지 않고 fs::path(string)을 바로 호출하면 한글이 깨집니다.
     * * @param utf8_str UTF-8 인코딩된 문자열 (예: "한글파일.jpg")
     * @return fs::path OS 호환 경로 객체
     */
    static fs::path ToPath(const std::string& utf8_str) {
        return fs::u8path(utf8_str);
    }

    /**
     * @brief [Helper] Windows Path -> UTF-8 String 변환
     * * 파일 시스템에서 읽은 경로(fs::path)를 로그 출력이나 HTTP 헤더 전송을 위해
     * 표준 UTF-8 문자열로 변환합니다.
     * .string() 함수는 시스템 로캘(ANSI/CP949)로 변환을 시도하다가
     * 표현 불가능한 문자(이모지 등)를 만나면 예외를 던지며 서버를 죽일 수 있으므로,
     * 반드시 이 함수를 통해 u8string() -> string 변환을 수행해야 합니다.
     * * @param p Windows 경로 객체
     * @return std::string UTF-8 인코딩된 문자열
     */
    static std::string PathToStr(const fs::path& p) {
        std::u8string u8 = p.u8string();
        return std::string(reinterpret_cast<const char*>(u8.c_str()));
    }

public:
    /**
     * @brief [Phase 2] NAS 폴더 다운로드 (재귀적 복사)
     * * NAS(Network Attached Storage)의 특정 폴더를 로컬 캐시 경로로 통째로 복사합니다.
     * * @param unc_path 원본 NAS 경로 (UNC Path, 예: \\\\192.168.0.1\\Data)
     * @param local_root 로컬 저장소 최상위 경로 (예: C:\\Temp\\Cache)
     * @param out_local_path [Out] 실제 저장된 로컬 경로 반환
     * @return true 성공 시, false 실패 시
     */
    static bool DownloadFolder(const std::string& unc_path, const std::string& local_root, std::string& out_local_path) {
        try {
            fs::path source = ToPath(unc_path); // 원본 경로 변환
            
            // 원본 존재 여부 확인
            if (!fs::exists(source)) {
                // PathToStr를 사용하여 경로가 깨지지 않게 로그 출력
                spdlog::error("[404] NAS Source not found: {}", PathToStr(source));
                return false;
            }

            // 대상 경로 생성: CacheRoot / 원본폴더명
            fs::path dest = ToPath(local_root) / source.filename();
            
            // 대상 폴더 생성 (이미 있으면 무시)
            fs::create_directories(dest);
            
            // 재귀적 복사 (하위 폴더 포함, 덮어쓰기 옵션)
            fs::copy(source, dest, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
            
            out_local_path = PathToStr(dest); // 결과 경로 반환
            return true;
        } catch (const std::exception& e) {
            spdlog::error("[IO Error] Download failed: {}", e.what());
            return false;
        }
    }

    /**
     * @brief [Phase 2] 폴더 삭제
     * * 지정된 폴더와 그 하위 모든 내용을 삭제합니다.
     * * @param full_path 삭제할 폴더의 전체 경로
     * @return true 삭제 성공 (또는 폴더가 없음), false 실패
     */
    static bool DeleteFolder(const std::string& full_path) {
        try {
            fs::path p = ToPath(full_path);
            if (fs::exists(p)) {
                fs::remove_all(p); // 폴더 및 하위 파일 전체 삭제
                return true;
            }
            return false; // 삭제할 폴더가 없음
        } catch (const std::exception& e) {
            spdlog::error("[IO Error] Delete failed: {}", e.what());
            return false;
        }
    }

    /**
     * @brief [Phase 5] Atomic File Write (안전한 파일 업로드)
     * * 파일을 저장할 때 '임시 파일(.tmp) 작성 -> 원본 이름으로 변경(Rename)' 방식을 사용하여,
     * 쓰기 도중 전원이 차단되거나 오류가 발생해도 원본 파일이 손상되지 않게 합니다.
     * * @param folder_path 저장될 폴더 경로
     * @param filename 저장할 파일명
     * @param content 파일 내용 (바이너리 데이터)
     * @return true 성공, false 실패
     */
    static bool SaveFileAtomic(const std::string& folder_path, const std::string& filename, const std::string& content) {
        try {
            fs::path dir = ToPath(folder_path);
            if (!fs::exists(dir)) fs::create_directories(dir);

            fs::path final_path = dir / ToPath(filename);      // 최종 저장 경로
            fs::path temp_path = dir / ToPath(filename + ".tmp"); // 임시 파일 경로

            // 1. 임시 파일 쓰기 (.tmp)
            {
                std::ofstream out(temp_path, std::ios::binary); // 바이너리 모드로 열기
                if (!out.is_open()) {
                    spdlog::error("[IO] Failed to open temp file: {}", PathToStr(temp_path));
                    return false;
                }
                out.write(content.data(), content.size());
            } // 여기서 ofstream이 닫히며(flush) 파일이 디스크에 기록됨

            // 2. Atomic Rename (임시 파일 -> 최종 파일)
            // OS 수준에서 원자적(Atomic)으로 처리되어 안전함
            fs::rename(temp_path, final_path);
            
            spdlog::info("[IO] Saved Atomic: {}", PathToStr(final_path));
            return true;
        } catch (const std::exception& e) {
            spdlog::error("[IO] Save Failed: {}", e.what());
            return false;
        }
    }

    /**
     * @brief [Phase 5] 파일 다운로드를 위한 경로 검증 및 반환
     * * 클라이언트가 요청한 상대 경로가 유효한지 검사하고, 실제 전체 경로를 반환합니다.
     * Path Traversal 공격(.. 문자 포함)을 방어합니다.
     * * @param local_root 로컬 캐시 최상위 경로
     * @param subpath 클라이언트가 요청한 상대 경로
     * @param out_full_path [Out] 검증된 전체 경로 (UTF-8 String)
     * @return true 유효한 파일임, false 파일이 없거나 잘못된 경로
     */
    static bool GetFileForDownload(const std::string& local_root, const std::string& subpath, std::string& out_full_path) {
        try {
            // [Security] 상위 폴더 접근(..) 시도 차단
            if (subpath.find("..") != std::string::npos) return false;

            fs::path p = ToPath(local_root) / ToPath(subpath);
            
            // 파일 존재 여부 및 디렉토리가 아닌지 확인
            if (fs::exists(p) && fs::is_regular_file(p)) {
                // [Crucial Fix] 여기서 .string()을 쓰면 한글 윈도우가 아닐 때 서버가 죽음
                // 반드시 PathToStr를 통해 UTF-8 문자열로 반환해야 함
                out_full_path = PathToStr(p);
                return true;
            }
            return false;
        } catch (...) { return false; }
    }
};