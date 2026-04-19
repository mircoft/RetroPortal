cmake_minimum_required(VERSION 3.22)
project(retroportal_native LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_library(LOG_LIB log)
find_library(ANDROID_LIB android)
find_library(DL_LIB dl)

set(RETROPORTAL_SOURCES
    src/NativeEngine.cpp
    src/VirtualMemoryManager.cpp
    src/ProcessHost.cpp
    src/gpu/CommandBuffer.cpp
    src/gpu/GpuBackend.cpp
    src/gpu/D3DTranslator.cpp
)

add_library(retroportal_native SHARED ${RETROPORTAL_SOURCES})

target_include_directories(retroportal_native PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_definitions(retroportal_native PRIVATE
    _GNU_SOURCE
)

if(ANDROID)
    target_compile_definitions(retroportal_native PRIVATE RETROPORTAL_ANDROID=1)
endif()

target_link_libraries(retroportal_native
    ${LOG_LIB}
    ${ANDROID_LIB}
    ${DL_LIB}
)

# Optional userfaultfd (typically off for Play retail builds; enable for engineering device images).
option(RETROPORTAL_ENABLE_USERFAULTFD "Compile userfaultfd guest paging path" OFF)
if(RETROPORTAL_ENABLE_USERFAULTFD)
    target_compile_definitions(retroportal_native PRIVATE RETROPORTAL_HAVE_USERFAULTFD=1)
endif()
