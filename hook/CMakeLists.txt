cmake_minimum_required(VERSION 3.15)
project(shadowrec_hook)
 
# C++ standardı
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
 
# 64-bit build (modern oyunlar)
set(CMAKE_GENERATOR_PLATFORM x64)
 
# ═══════════════════════════════════════════════════════════════════════
# HOOK DLL - Oyun process'ine inject edilecek olan
# ═══════════════════════════════════════════════════════════════════════
add_library(shadowrec_hook SHARED
    dllmain.cpp
)
 
target_link_libraries(shadowrec_hook PRIVATE
    user32
    kernel32
)
 
set_target_properties(shadowrec_hook PROPERTIES
    PREFIX ""                          # 'lib' prefix'i istemiyoruz
    OUTPUT_NAME "shadowrec_hook"       # shadowrec_hook.dll olacak
)
 
# ═══════════════════════════════════════════════════════════════════════
# INJECTOR EXE - DLL'i hedef process'e inject eder
# ═══════════════════════════════════════════════════════════════════════
add_executable(injector
    injector.cpp
)
 
target_link_libraries(injector PRIVATE
    user32
    kernel32
)
 