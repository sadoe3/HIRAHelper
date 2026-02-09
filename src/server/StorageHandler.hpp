#pragma once
#include <filesystem>
#include <string>
#include <spdlog/spdlog.h>

namespace fs = std::filesystem;

class StorageHandler {
public:
    // unc_path: \\127.0.0.1\PacsShort\Study01\Series001
    // local_root: C:\Temp\HiraCache
    static bool DownloadFolder(const std::string& unc_path, const std::string& local_root, std::string& out_local_path) {
        try {
            fs::path source(unc_path);
            
            // source가 존재하지 않으면 실패
            if (!fs::exists(source)) {
                spdlog::error("[404] NAS Source not found: {}", source.string());
                return false;
            }

            // 목적지: C:\Temp\HiraCache\Series001 (마지막 폴더명 사용)
            fs::path dest = fs::path(local_root) / source.filename();

            // 폴더 생성 (상위 폴더 포함)
            fs::create_directories(dest);

            // 재귀 복사 (덮어쓰기 옵션)
            fs::copy(source, dest, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
            
            out_local_path = dest.string();
            spdlog::info("[IO] Download Success: {} -> {}", source.string(), dest.string());
            return true;
        } catch (const std::exception& e) {
            spdlog::error("[IO Error] Download failed: {}", e.what());
            return false;
        }
    }

    static bool DeleteFolder(const std::string& full_path) {
        try {
            fs::path p(full_path);
            if (fs::exists(p)) {
                fs::remove_all(p); // 하위 파일 포함 전체 삭제
                spdlog::info("[IO] Deleted: {}", p.string());
                return true;
            }
            return false;
        } catch (const std::exception& e) {
            spdlog::error("[IO Error] Delete failed: {}", e.what());
            return false;
        }
    }
};