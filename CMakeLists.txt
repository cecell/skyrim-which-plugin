cmake_minimum_required(VERSION 3.21)

# The one place for the version; the release workflow reads it from this line.
project(WhichPlugin VERSION 0.91 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(COMMONLIB_DIR "C:/repos/CommonLibSSE-NG" CACHE PATH "CommonLibSSE-NG source checkout")
if(NOT EXISTS "${COMMONLIB_DIR}/CMakeLists.txt")
	include(FetchContent)
	# SOURCE_SUBDIR names nothing, so this only downloads; add_subdirectory below does the rest.
	FetchContent_Declare(commonlib
		GIT_REPOSITORY https://github.com/alandtse/CommonLibSSE-NG.git
		GIT_TAG 5decf47b01dde5501b03afaa91cd4d182e793cca
		SOURCE_SUBDIR no-cmake-here)
	FetchContent_MakeAvailable(commonlib)
	set(COMMONLIB_DIR "${commonlib_SOURCE_DIR}")
endif()

# SE + AE in one DLL; VR is a separate game we don't target.
set(ENABLE_SKYRIM_SE ON CACHE BOOL " " FORCE)
set(ENABLE_SKYRIM_AE ON CACHE BOOL " " FORCE)
set(ENABLE_SKYRIM_VR OFF CACHE BOOL " " FORCE)
set(BUILD_TESTS OFF CACHE BOOL " " FORCE)

add_subdirectory(${COMMONLIB_DIR} CommonLibSSE EXCLUDE_FROM_ALL)
include(${COMMONLIB_DIR}/cmake/CommonLibSSE.cmake)

add_commonlibsse_plugin(${PROJECT_NAME}
	AUTHOR "Cecell"
	SOURCES
		src/main.cpp
		src/Open.cpp
		src/Settings.cpp
		src/ConsoleCommands.cpp
		src/ConsoleHook.cpp
		src/LoadOrder.cpp
		src/Pipeline.cpp
		src/Query.cpp
		src/Redirect.cpp
		src/TextTools.cpp
		src/Variables.cpp
		src/WhichPlugin.cpp
)
find_package(nlohmann_json CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE nlohmann_json::nlohmann_json)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
target_compile_definitions(${PROJECT_NAME} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE
	PLUGIN_VERSION_STRING="${PROJECT_VERSION}")
target_precompile_headers(${PROJECT_NAME} PRIVATE include/PCH.h)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Unit tests for the parts that don't need the game (parser, pipe splitter, text tools).
enable_testing()
add_executable(wp_tests tests/tests.cpp src/Query.cpp src/Pipeline.cpp src/TextTools.cpp)
target_compile_features(wp_tests PRIVATE cxx_std_23)
target_precompile_headers(wp_tests PRIVATE tests/PCH.h)
target_include_directories(wp_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
add_test(NAME wp_tests COMMAND wp_tests)
