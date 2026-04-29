cmake_minimum_required(VERSION 3.16)

project(HighFreqOrderMatching VERSION 1.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(GNUInstallDirs)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/hfom/version.hpp"
    @ONLY
)

include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/archive/refs/tags/v3.11.3.zip
)
FetchContent_MakeAvailable(nlohmann_json)

add_library(hfom_orderbook STATIC src/orderbook.cpp)
target_include_directories(hfom_orderbook
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>"
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

add_library(hfom_replay STATIC src/replay_formats.cpp)
target_link_libraries(hfom_replay PUBLIC hfom_orderbook nlohmann_json::nlohmann_json)

add_executable(match_cli src/main.cpp)
target_link_libraries(match_cli PRIVATE hfom_replay)
target_include_directories(match_cli PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")

include(CMakePackageConfigHelpers)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/HighFreqOrderMatchingConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/HighFreqOrderMatchingConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/HighFreqOrderMatching"
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/HighFreqOrderMatchingConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion
)

install(TARGETS hfom_orderbook
    EXPORT HighFreqOrderMatchingTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

install(FILES src/orderbook.h DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/generated/hfom/version.hpp"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/hfom")

install(TARGETS match_cli RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

install(EXPORT HighFreqOrderMatchingTargets
    FILE HighFreqOrderMatchingTargets.cmake
    NAMESPACE HighFreqOrderMatching::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/HighFreqOrderMatching"
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/HighFreqOrderMatchingConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/HighFreqOrderMatchingConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/HighFreqOrderMatching"
)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    option(BUILD_HFOM_TESTS "Build HighFreqOrderMatching unit tests" ON)
else()
    option(BUILD_HFOM_TESTS "Build HighFreqOrderMatching unit tests" OFF)
endif()

if(BUILD_HFOM_TESTS)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    enable_testing()
    add_subdirectory(tests)
endif()
