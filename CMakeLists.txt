cmake_minimum_required(VERSION 3.14)
project(MediaScanner)

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pthread -O3")

find_package(Threads REQUIRED)

include(FetchContent)
FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.11.2/json.tar.xz
)
FetchContent_MakeAvailable(json)

add_executable(media_scanner 
    src/main.cpp 
    src/scanner.cpp 
    src/server.cpp
)

target_link_libraries(media_scanner PRIVATE Threads::Threads nlohmann_json::nlohmann_json)
