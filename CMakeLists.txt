cmake_minimum_required(VERSION 3.15)
project(hip_gl_interop LANGUAGES CXX)

if(DEFINED ENV{ROCM_PATH})
    set(ROCM_PATH $ENV{ROCM_PATH} CACHE PATH "Path to ROCm installation")
else()
    set(ROCM_PATH "/opt/rocm" CACHE PATH "Path to ROCm installation")
endif()

set(CMAKE_CXX_COMPILER "${ROCM_PATH}/bin/hipcc")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)

add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${ROCM_PATH}/include
    ${CMAKE_SOURCE_DIR}/include
    src
)

target_link_directories(${PROJECT_NAME} PRIVATE
    ${ROCM_PATH}/lib
    ${CMAKE_SOURCE_DIR}/lib
)

target_link_libraries(${PROJECT_NAME}
    raylib GL pthread m
)

