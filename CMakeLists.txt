cmake_minimum_required(VERSION 3.20)

project(HiraHelperSolution VERSION 2.0.0 LANGUAGES CXX)

# [추가됨] Windows 10 이상을 타겟으로 설정 (ASIO 경고 해결)
add_compile_definitions(_WIN32_WINNT=0x0A00) 

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 패키지 찾기
find_package(Crow CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(cpr CONFIG REQUIRED)

# -------------------------------------------------------
# Target 1: HIRA Helper (Server)
# -------------------------------------------------------
add_executable(HiraHelper src/server/main.cpp)
target_link_libraries(HiraHelper PRIVATE 
    Crow::Crow 
    spdlog::spdlog 
    nlohmann_json::nlohmann_json
    # 중요: 여기에는 cpr::cpr이 없습니다. 서버는 cpr을 쓰지 않기 때문입니다.
)

# -------------------------------------------------------
# Target 2: Test Client (Client)
# -------------------------------------------------------
add_executable(TestClient src/client/main.cpp)
target_link_libraries(TestClient PRIVATE 
    cpr::cpr
    spdlog::spdlog
    nlohmann_json::nlohmann_json
)