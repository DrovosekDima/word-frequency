cmake_minimum_required(VERSION 3.15)
project(WordsFrequency)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(spdlog CONFIG REQUIRED)
find_package(Threads REQUIRED)

add_executable(word_frequency 
    sha256.cpp
    WordsFrequency.cpp
)

target_link_libraries(word_frequency PRIVATE spdlog::spdlog Threads::Threads)
target_include_directories(word_frequency PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(MSVC)
    target_compile_options(word_frequency PRIVATE /utf-8)
endif()