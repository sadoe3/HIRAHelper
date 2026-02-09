#include <iostream>
#include <spdlog/spdlog.h>
#include <crow.h> 

// 주의: 여기에 #include <cpr/cpr.h>가 있으면 안 됩니다!

int main() {
    // 1. 서버 시작 로그
    spdlog::info("[HiraHelper] Server Starting...");

    // 2. Crow 앱 인스턴스 (서버 로직)
    crow::SimpleApp app;

    // 3. 테스트용 라우트
    CROW_ROUTE(app, "/")([](){
        return "HIRA Helper Agent is Running.";
    });

    // 4. 포트 설정 및 비동기 실행 방지 (Block)
    // 개발 단계에서는 8080 포트 사용
    spdlog::info("[HiraHelper] Listening on port 8080");
    app.port(8080).multithreaded().run();
    
    return 0;
}