cmake_minimum_required(VERSION 3.15)
project(RdpSessionManager CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enforce Unicode for Windows APIs
add_compile_definitions(UNICODE _UNICODE)

add_executable(RdpSessionManager WIN32
    src/main.cpp
    src/PowerMonitor.cpp
    src/RdpController.cpp
    src/ArtifactCleaner.cpp
    src/Logger.cpp
)

target_link_libraries(RdpSessionManager PRIVATE 
    user32 
    advapi32 
    powrprof 
    Wtsapi32
    shell32
)

target_include_directories(RdpSessionManager PRIVATE include)