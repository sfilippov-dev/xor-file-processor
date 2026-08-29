cmake_minimum_required(VERSION 3.16)

project(xor-file-processor VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Совместимо с Qt6 и Qt5 (в вакансии — Qt Creator/MinGW; берётся доступная версия).
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

add_executable(xor-file-processor
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/FileProcessor.cpp
    src/FileProcessor.h
)

target_link_libraries(xor-file-processor PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)

# На Windows — оконное приложение без консоли.
if(WIN32)
    set_target_properties(xor-file-processor PROPERTIES WIN32_EXECUTABLE ON)
endif()
