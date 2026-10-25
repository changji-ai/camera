cmake_minimum_required(VERSION 3.21)

# keep project() on ONE line - tools/bump_version.sh and CI parse the version
# from it (cmake_minimum_required also contains a VERSION keyword)
project(camera VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Multimedia)
qt_standard_project_setup()

# ---------------- ONNX Runtime ----------------
set(ORT_VERSION "1.20.1" CACHE STRING "onnxruntime release version")

if(NOT ONNXRUNTIME_ROOT AND DEFINED ENV{ONNXRUNTIME_ROOT})
    set(ONNXRUNTIME_ROOT "$ENV{ONNXRUNTIME_ROOT}")
endif()

if(NOT ONNXRUNTIME_ROOT)
    set(ORT_DIR "${CMAKE_BINARY_DIR}/deps/onnxruntime")
    if(WIN32)
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM64|arm64|aarch64|AARCH64)$")
            set(ORT_PKG "onnxruntime-win-arm64-${ORT_VERSION}.zip")
        else()
            set(ORT_PKG "onnxruntime-win-x64-${ORT_VERSION}.zip")
        endif()
    elseif(APPLE)
        if(CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
            set(ORT_PKG "onnxruntime-osx-arm64-${ORT_VERSION}.tgz")
        else()
            set(ORT_PKG "onnxruntime-osx-x86_64-${ORT_VERSION}.tgz")
        endif()
    else()
        if(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
            set(ORT_PKG "onnxruntime-linux-aarch64-${ORT_VERSION}.tgz")
        else()
            set(ORT_PKG "onnxruntime-linux-x64-${ORT_VERSION}.tgz")
        endif()
    endif()
    string(REGEX REPLACE "\\.(zip|tgz)$" "" ORT_BASE "${ORT_PKG}")
    set(ONNXRUNTIME_ROOT "${ORT_DIR}/${ORT_BASE}")
    if(NOT EXISTS "${ONNXRUNTIME_ROOT}/include/onnxruntime_cxx_api.h")
        set(ORT_URL "https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/${ORT_PKG}")
        message(STATUS "Downloading onnxruntime: ${ORT_URL}")
        file(DOWNLOAD "${ORT_URL}" "${ORT_DIR}/${ORT_PKG}" SHOW_PROGRESS)
        file(ARCHIVE_EXTRACT INPUT "${ORT_DIR}/${ORT_PKG}" DESTINATION "${ORT_DIR}")
        file(REMOVE "${ORT_DIR}/${ORT_PKG}")
    endif()
endif()

find_path(ORT_INCLUDE_DIR onnxruntime_cxx_api.h
    HINTS "${ONNXRUNTIME_ROOT}/include" REQUIRED)

if(WIN32)
    # win-x64 / win-arm64 zip: import lib in lib/, dll in lib/ (1.20 layout has dll in lib too)
    find_library(ORT_IMPLIB onnxruntime HINTS "${ONNXRUNTIME_ROOT}/lib" REQUIRED)
    find_file(ORT_RUNTIME onnxruntime.dll
        HINTS "${ONNXRUNTIME_ROOT}/lib" "${ONNXRUNTIME_ROOT}/bin" REQUIRED)
else()
    find_library(ORT_LIBRARY onnxruntime HINTS "${ONNXRUNTIME_ROOT}/lib" REQUIRED)
    get_filename_component(ORT_RUNTIME "${ORT_LIBRARY}" ABSOLUTE)
endif()

add_library(ort SHARED IMPORTED GLOBAL)
set_target_properties(ort PROPERTIES
    IMPORTED_LOCATION "${ORT_RUNTIME}"
    IMPORTED_IMPLIB "${ORT_IMPLIB}")
# MinGW cannot reliably use the MSVC import lib: link directly against the DLL.
if(WIN32 AND NOT MSVC)
    set_target_properties(ort PROPERTIES IMPORTED_IMPLIB "${ORT_RUNTIME}")
endif()

# ---------------- application ----------------
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_executable(camera
    src/main.cpp
    src/MainWindow.h src/MainWindow.cpp
    src/VideoWidget.h src/VideoWidget.cpp
    src/YoloDetector.h src/YoloDetector.cpp)

target_include_directories(camera PRIVATE src ${ORT_INCLUDE_DIR})
target_link_libraries(camera PRIVATE Qt6::Widgets Qt6::Multimedia ort)
target_compile_definitions(camera PRIVATE CAMERA_VERSION="${PROJECT_VERSION}")

set_target_properties(camera PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE TRUE)

# keep the bundled model next to the binary for development runs
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/models")
    add_custom_command(TARGET camera POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/models" "$<TARGET_FILE_DIR:camera>/models")
endif()

install(TARGETS camera BUNDLE DESTINATION . RUNTIME DESTINATION bin)

# headless smoke test: model load + full inference pass without a camera
enable_testing()
add_executable(camera-smoke tests/smoke.cpp src/YoloDetector.cpp)
target_include_directories(camera-smoke PRIVATE src ${ORT_INCLUDE_DIR})
target_link_libraries(camera-smoke PRIVATE Qt6::Gui ort)
add_test(NAME smoke COMMAND camera-smoke)
