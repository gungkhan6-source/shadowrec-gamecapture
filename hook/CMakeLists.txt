cmake_minimum_required(VERSION 3.15)
project(shadowrec_hook)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_GENERATOR_PLATFORM x64)

# MinHook
set(MINHOOK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/minhook)

add_library(minhook STATIC
    ${MINHOOK_DIR}/src/buffer.c
    ${MINHOOK_DIR}/src/hook.c
    ${MINHOOK_DIR}/src/trampoline.c
    ${MINHOOK_DIR}/src/hde/hde32.c
    ${MINHOOK_DIR}/src/hde/hde64.c
)

target_include_directories(minhook PUBLIC ${MINHOOK_DIR}/include)

# Hook DLL — Universal (DX9 + DX10 + DX11 + DX11.1 + DX12 + OpenGL)
add_library(shadowrec_hook SHARED dllmain.cpp)

target_include_directories(shadowrec_hook PRIVATE ${MINHOOK_DIR}/include)

target_link_libraries(shadowrec_hook PRIVATE
    minhook
    user32
    kernel32
    d3d9
    d3d11
    d3d12
    dxgi
    opengl32
)

set_target_properties(shadowrec_hook PROPERTIES
    PREFIX ""
    OUTPUT_NAME "shadowrec_hook"
)

# Injector
add_executable(injector injector.cpp)
target_link_libraries(injector PRIVATE user32 kernel32)

# Test app (DX11)
add_executable(test_app WIN32 test-app.cpp)
target_link_libraries(test_app PRIVATE d3d11 dxgi user32)

# SHM reader
add_executable(shm_reader shm_reader.cpp)
target_link_libraries(shm_reader PRIVATE user32 kernel32)
