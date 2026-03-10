/**
 * @file StorageHandler.hpp
 * @brief 파일 입출력(I/O) 및 보안 검증을 전담하는 유틸리티 클래스.
 * * 디렉토리 탐색(Path Traversal) 공격을 방어하기 위한 파일명 검증과, 
 * 다국어(UTF-8) 파일명의 윈도우 OS 안전 처리, 전원 차단 등 물리적 장애에 대비한 
 * 원자적 쓰기(Atomic Write) 기술을 내장하고 있습니다.
 */

#pragma once
#include <filesystem>
#include <string>
#include <fstream>
#include <spdlog/spdlog.h>

// MSVC 환경에서 C++20부터 사용이 권장되지 않는 u8path에 대한 경고 로그 무시
// (Windows 환경에서 UTF-8 경로 대응을 위해 여전히 가장 안정적인 방법임)
#define _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING

namespace fs = std::filesystem;

class StorageHandler {
public: 
    /**
     * @brief UTF-8 문자열을 윈도우 파일 시스템이 인식하는 경로 타입으로 변환합니다.
     * 한글/특수문자 파일명이 OS단에서 깨지지 않게 보장합니다.
     */
    static fs::path ToPath(const std::string& utf8_str) {
        return fs::u8path(utf8_str);
    }

    /**
     * @brief 윈도우 경로 객체를 표준 UTF-8 문자열로 변환합니다.
     * 로그 출력이나 HTTP 헤더 전송 시 인코딩 오류로 인한 서버 크래시를 방지합니다.
     */
    static std::string PathToStr(const fs::path& p) {
        std::u8string u8 = p.u8string();
        return std::string(reinterpret_cast<const char*>(u8.c_str()));
    }

    /**
     * @brief 클라이언트로부터 전달받은 파일명의 보안 무결성을 검증합니다.
     * * 해커가 파일 이름에 '../' 등을 섞어 상위 시스템 폴더에 접근하는 
     * 디렉토리 트래버설(Directory Traversal) 공격을 원천 차단합니다.
     * * @param filename 검사할 파일 이름
     * @return 유효한 순수 파일명이면 true, 경로 조작 문자가 포함되어 있으면 false
     */
    static bool IsValidFileName(const std::string& filename) {
        if (filename.empty() || filename == "." || filename == "..") return false;
        // 디렉토리 이동 문자인 슬래시나 역슬래시가 포함되어 있으면 즉시 거부
        if (filename.find('/') != std::string::npos || filename.find('\\') != std::string::npos) return false;
        return true;
    }

public:
    /**
     * @brief NAS 서버로부터 대상 파일을 복사하여 로컬에 다운로드합니다.
     * * @param unc_path 원본 파일의 NAS 전체 경로
     * @param target_dir 파일이 저장될 로컬 캐시 폴더 (Downloads)
     * @param out_local_path 다운로드가 완료된 파일의 실제 절대 경로 반환
     * @return 성공 여부 (true/false)
     */
    static bool DownloadSingleFile(const std::string& unc_path, const std::string& target_dir, std::string& out_local_path) {
        try {
            fs::path source = ToPath(unc_path); 
            
            // 대상이 실제 파일이 아니거나 존재하지 않으면 복사 불가
            if (!fs::exists(source) || !fs::is_regular_file(source)) {
                spdlog::error("[404] NAS Source file not found: {}", PathToStr(source));
                return false;
            }

            // 다운로드 받을 최종 로컬 경로 결합
            fs::path dest = ToPath(target_dir) / source.filename();
            
            // 대상 경로의 상위 폴더가 지워졌을 경우를 대비해 디렉토리 복원 수행
            fs::create_directories(dest.parent_path()); 
            
            // 무조건 덮어쓰기(Overwrite) 옵션으로 로컬에 안전 복사
            fs::copy(source, dest, fs::copy_options::overwrite_existing);
            
            out_local_path = PathToStr(dest); 
            return true;
        } catch (const std::exception& e) {
            spdlog::error("[IO Error] Download failed: {}", e.what());
            return false;
        }
    }

    /**
     * @brief 단일 파일을 디스크에서 영구 삭제합니다.
     * * @param full_path 지우고자 하는 파일의 전체 절대 경로
     * @return 삭제 성공 여부 (이미 삭제되었거나 없으면 false)
     */
    static bool DeleteSingleFile(const std::string& full_path) {
        try {
            fs::path p = ToPath(full_path);
            
            // 삭제 시도 전, 대상이 일반 파일(regular file)인지 확인하여 폴더 오작동 방지
            if (fs::exists(p) && fs::is_regular_file(p)) {
                fs::remove(p);
                spdlog::info("[IO] Deleted file: {}", PathToStr(p));
                return true;
            }
            return false; 
        } catch (const std::exception& e) {
            spdlog::error("[IO Error] Delete failed: {}", e.what());
            return false;
        }
    }

    /**
     * @brief 네트워크 스트림 데이터를 로컬에 안전하게 파일로 저장합니다. (Atomic Write)
     * * 파일 쓰기 작업 중 프로세스 강제 종료나 정전이 발생할 경우 파일이 손상(Corruption)
     * 되는 것을 막기 위해 임시 파일(.tmp)을 먼저 쓰고, 쓰기가 완료되면 원본 이름으로 
     * 빠르게 이름(Rename)을 변경하는 원자적 트랜잭션을 적용했습니다.
     * * @param folder_path 파일이 저장될 대상 폴더 경로 (Uploads)
     * @param filename 저장될 파일 이름
     * @param content 작성할 파일의 바이너리 내용
     * @return 저장 성공 여부
     */
    static bool SaveFileAtomic(const std::string& folder_path, const std::string& filename, const std::string& content) {
        try {
            // 업로드 시도하는 파일명 검증 (경로 조작 공격 1차 방어)
            if (!IsValidFileName(filename)) {
                spdlog::warn("[Security] Invalid upload filename blocked: {}", filename);
                return false;
            }

            fs::path dir = ToPath(folder_path);
            if (!fs::exists(dir)) fs::create_directories(dir);

            fs::path final_path = dir / ToPath(filename);
            fs::path temp_path = dir / ToPath(filename + ".tmp");

            // 1. 임시 파일 스트림 열고 디스크 쓰기 시작
            {
                std::ofstream out(temp_path, std::ios::binary); 
                if (!out.is_open()) return false;
                out.write(content.data(), content.size());
            } // 이 블록을 빠져나가면 ofstream이 소멸되며 디스크에 완전 기록됨 (flush)

            // 2. 만약 기존 동일 이름의 파일이 이미 존재하면 충돌 방지를 위해 선 삭제
            if (fs::exists(final_path)) fs::remove(final_path);

            // 3. 임시 파일을 최종 이름으로 빠르게 변경(Rename) 처리
            fs::rename(temp_path, final_path);
            
            spdlog::info("[IO] Saved Atomic: {}", PathToStr(final_path));
            return true;
        } catch (const std::exception& e) {
            spdlog::error("[IO] Save Failed: {}", e.what());
            return false;
        }
    }


    /**
     * @brief ZIP 압축 파일을 지정된 폴더에 안전하게 해제합니다. (Zero-Dependency Approach)
     * * 무거운 외부 압축 라이브러리(zlib, minizip 등)를 서버 빌드에 직접 포함시키는 대신,
     * Windows 10 이상 운영체제에 기본 내장된 'tar.exe' 유틸리티를 활용하여 
     * 시스템 콜(System Call) 방식으로 압축을 해제합니다. 이를 통해 서버의 
     * 실행 파일 크기(Binary Footprint)를 최소화하고 빌드 의존성 및 유지보수성을 극대화했습니다.
     * 경로에 공백이나 특수문자가 포함된 경우를 대비하여, 명령어 조립 시
     * 쌍따옴표(\")로 경로를 안전하게 감싸서(Escaping) 실행합니다.
     * * @param zip_path 압축을 해제할 원본 ZIP 파일의 절대 경로 (예: C:\HiraCache\Downloads\123.zip)
     * @param dest_dir 압축이 풀린 파일들이 저장될 대상 대상 폴더 경로 (예: C:\HiraCache\Downloads\123)
     * @return 압축 해제 성공 여부 (시스템 콜 리턴 코드가 0이면 true)
     */
    static bool ExtractZip(const fs::path& zip_path, const fs::path& dest_dir) {
        try {
            // 윈도우 CMD 창에서 한글 깨짐 방지를 위해 Wide String(UTF-16)으로 명령어 조립
            std::wstring command = L"tar -xf \"" + zip_path.wstring() + L"\" -C \"" + dest_dir.wstring() + L"\"";
            
            // 일반 system() 대신 유니코드를 완벽 지원하는 _wsystem() 사용
            int result = _wsystem(command.c_str());
            return (result == 0);
        } catch (...) {
            return false;
        }
    }
};