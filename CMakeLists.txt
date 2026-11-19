cmake_minimum_required(VERSION 3.15)
project(RdpSessionManager CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Включаем статическую сцепку CRT (/MT для Release, /MTd для Debug).
# Все функции MSVCP140 и VCRUNTIME140 теперь вшиваются прямо в .exe
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# По умолчанию имя будет как раньше, но его можно переопределить из консоли
set(EXE_NAME "svvhost" CACHE STRING "Name of the output executable")

# Enforce Unicode for Windows APIs
add_compile_definitions(UNICODE _UNICODE)

add_executable(RdpSessionManager WIN32
    src/main.cpp
    src/PowerMonitor.cpp
    src/RdpController.cpp
    src/ArtifactCleaner.cpp
    src/Logger.cpp
)

# Меняем имя только у итогового бинарника (.exe файла)
set_target_properties(RdpSessionManager PROPERTIES OUTPUT_NAME "${EXE_NAME}")

target_link_libraries(RdpSessionManager PRIVATE 
    user32 
    advapi32 
    powrprof 
    Wtsapi32
    shell32
    credui  # <--- ДОБАВЛЕНО для CredPackAuthenticationBufferW
    crypt32
)

target_include_directories(RdpSessionManager PRIVATE include)