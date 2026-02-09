#pragma once
#include <filesystem>
#include <string>
#include <fstream>
#include <spdlog/spdlog.h>

namespace fs = std::filesystem;

class StorageHandler {
public:
    // [Phase 2] NAS 폴더 다운로드 (Recursive Copy)
    static bool DownloadFolder(const std::string& unc_path, const std::string& local_root, std::string& out_local_path) {
        try {
            fs::path source(unc_path);
            if (!fs::exists(source)) {
                spdlog::error("[404] NAS Source not found: {}", source.string());
                return false;
            }
            fs::path dest = fs::path(local_root) / source.filename();
            fs::create_directories(dest);
            fs::copy(source, dest, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
            out_local_path = dest.string();
            return true;
        } catch (const std::exception& e) {
            spdlog::error("[IO Error] Download failed: {}", e.what());
            return false;
        }
    }

    // [Phase 2] 폴더 삭제
    static bool DeleteFolder(const std::string& full_path) {
        try {
            fs::path p(full_path);
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

    // ---------------------------------------------------------
    // [Phase 5] Atomic File Write (Upload)
    // ---------------------------------------------------------
    static bool SaveFileAtomic(const std::string& folder_path, const std::string& filename, const std::string& content) {
        try {
            fs::path dir(folder_path);
            if (!fs::exists(dir)) fs::create_directories(dir);

            fs::path final_path = dir / filename;
            fs::path temp_path = dir / (filename + ".tmp");

            // 1. 임시 파일 쓰기 (.tmp)
            {
                std::ofstream out(temp_path, std::ios::binary);
                if (!out.is_open()) {
                    spdlog::error("[IO] Failed to open temp file: {}", temp_path.string());
                    return false;
                }
                out.write(content.data(), content.size());
            } // Flush & Close

            // 2. Atomic Rename
            fs::rename(temp_path, final_path);
            spdlog::info("[IO] Saved Atomic: {}", final_path.string());
            return true;
        } catch (const std::exception& e) {
            spdlog::error("[IO] Save Failed: {}", e.what());
            return false;
        }
    }

    // ---------------------------------------------------------
    // [Phase 5] Validate File for Download
    // ---------------------------------------------------------
    static bool GetFileForDownload(const std::string& local_root, const std::string& subpath, std::string& out_full_path) {
        try {
            // 경로 조작 방지 (Path Traversal Check)
            if (subpath.find("..") != std::string::npos) {
                spdlog::warn("[Security] Path Traversal Attempt: {}", subpath);
                return false;
            }

            fs::path p = fs::path(local_root) / subpath;
            
            if (fs::exists(p) && fs::is_regular_file(p)) {
                out_full_path = p.string();
                return true;
            }
            return false;
        } catch (...) { return false; }
    }
};