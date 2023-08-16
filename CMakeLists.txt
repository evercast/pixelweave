cmake_minimum_required(VERSION 3.21 FATAL_ERROR)

cmake_policy(SET CMP0126 NEW)

set(VERSION 0.0.1)
set(PROJECT_NAME PixelWeave)

if(UNIX AND NOT APPLE)
    set(LINUX YES)
endif()

# Must be set before invoking project()
set(CMAKE_OSX_ARCHITECTURES arm64 x86_64)
set(CMAKE_OSX_DEPLOYMENT_TARGET 10.13)

# Build files in the top directory
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

project(${PROJECT_NAME} VERSION ${VERSION} LANGUAGES CXX)

# Add library subproject
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/lib)

# Add test subproject
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/test)

# Add benchmark subproject
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/benchmark)

# Configure 'Tests' as startup project
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT Tests)
