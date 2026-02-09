#pragma once
#include <filesystem>
#include <string>
#include <fstream>
#include <spdlog/spdlog.h>

// [Fix] C++20 u8path 경고 무시
#define _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING

namespace fs = std::filesystem;

class StorageHandler {
public: // [Change] main.cpp에서도 쓸 수 있게 public으로 변경
    // [Helper] UTF-8 String -> Windows Path
    static fs::path ToPath(const std::string& utf8_str) {
        return fs::u8path(utf8_str);
    }

    // [Helper] Windows Path -> UTF-8 String (로그/헤더용)
    static std::string PathToStr(const fs::path& p) {
        // .string()은 ANSI로 변환하려다 죽으므로, .u8string()을 써야 함
        std::u8string u8 = p.u8string();
        return std::string(reinterpret_cast<const char*>(u8.c_str()));
    }

public:
    // [Phase 2] NAS 폴더 다운로드
    static bool DownloadFolder(const std::string& unc_path, const std::string& local_root, std::string& out_local_path) {
        try {
            fs::path source = ToPath(unc_path);
            if (!fs::exists(source)) {
                spdlog::error("[404] NAS Source not found: {}", PathToStr(source));
                return false;
            }
            fs::path dest = ToPath(local_root) / source.filename();
            
            fs::create_directories(dest);
            fs::copy(source, dest, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
            
            out_local_path = PathToStr(dest);
            return true;
        } catch (const std::exception& e) {
            spdlog::error("[IO Error] Download failed: {}", e.what());
            return false;
        }
    }

    // [Phase 2] 폴더 삭제
    static bool DeleteFolder(const std::string& full_path) {
        try {
            fs::path p = ToPath(full_path);
            if (fs::exists(p)) {
                fs::remove_all(p);
                return true;
            }
            return false;
        } catch (const std::exception& e) {
            spdlog::error("[IO Error] Delete failed: {}", e.what());
            return false;
        }
    }

    // [Phase 5] Atomic File Write (Upload)
    static bool SaveFileAtomic(const std::string& folder_path, const std::string& filename, const std::string& content) {
        try {
            fs::path dir = ToPath(folder_path);
            if (!fs::exists(dir)) fs::create_directories(dir);

            fs::path final_path = dir / ToPath(filename);
            fs::path temp_path = dir / ToPath(filename + ".tmp");

            // 1. 임시 파일 쓰기 (.tmp)
            {
                std::ofstream out(temp_path, std::ios::binary);
                if (!out.is_open()) {
                    spdlog::error("[IO] Failed to open temp file: {}", PathToStr(temp_path));
                    return false;
                }
                out.write(content.data(), content.size());
            }

            // 2. Atomic Rename
            fs::rename(temp_path, final_path);
            
            spdlog::info("[IO] Saved Atomic: {}", PathToStr(final_path));
            return true;
        } catch (const std::exception& e) {
            spdlog::error("[IO] Save Failed: {}", e.what());
            return false;
        }
    }

    // [Phase 5] Validate File for Download
    static bool GetFileForDownload(const std::string& local_root, const std::string& subpath, std::string& out_full_path) {
        try {
            if (subpath.find("..") != std::string::npos) return false;

            fs::path p = ToPath(local_root) / ToPath(subpath);
            
            if (fs::exists(p) && fs::is_regular_file(p)) {
                // [Crucial Fix] 여기서 .string()을 쓰면 한글 윈도우가 아닐 때 서버가 죽음
                out_full_path = PathToStr(p);
                return true;
            }
            return false;
        } catch (...) { return false; }
    }
};