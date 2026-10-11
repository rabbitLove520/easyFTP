cmake_minimum_required(VERSION 3.10)
project(easyFTP VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra)

add_executable(easyftp
    src/main.cpp
    src/ftp_socket.cpp
    src/ftp_client.cpp
    src/ftp_command.cpp
    src/ftp_transfer.cpp
    src/utils.cpp
    src/line_editor.cpp
)

target_include_directories(easyftp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

find_package(Threads REQUIRED)
target_link_libraries(easyftp PRIVATE Threads::Threads readline)
