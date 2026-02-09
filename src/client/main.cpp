#include <iostream>
#include <spdlog/spdlog.h>
// Crow는 헤더 온리지만 컴파일 확인을 위해 포함
#include <crow.h> 

int main() {
    spdlog::info("[HiraHelper] Server Environment Ready.");
    spdlog::info("[System] Waiting for Phase 2 implementation...");
    
    // Crow 앱 인스턴스화 테스트 (실행은 하지 않음)
    crow::SimpleApp app;
    return 0;
}