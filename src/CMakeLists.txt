cmake_minimum_required(VERSION 3.20)
project(astraea)

include(ExternalProject)

option(COMPILE_INFERENCE_SERVICE "Compile Astraea inference services" OFF)

add_compile_options(-std=c++17 -Wall -pedantic -Wextra -Weffc++ -g)
# export compile_commands.json for clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# settings
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_INCLUDE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/include)

# check c++ headers & generate config.h
include(CheckIncludeFileCXX)
CHECK_INCLUDE_FILE_CXX(experimental/filesystem HAVE_EXPERIMENTAL_FILESYSTEM)
CHECK_INCLUDE_FILE_CXX(filesystem HAVE_FILESYSTEM)
configure_file(config.h.in ${CMAKE_INCLUDE_OUTPUT_DIRECTORY}/config.h)

# add json
set(JSON_DIR ${CMAKE_SOURCE_DIR}/../third_party/json/)
add_subdirectory(${JSON_DIR} ${CMAKE_BINARY_DIR}/json)

# batch inference service
if(COMPILE_INFERENCE_SERVICE)
    add_subdirectory(inference)
endif()

# include directory
include_directories(./net ${JSON_DIR}/single_include/nlohmann ${CMAKE_INCLUDE_OUTPUT_DIRECTORY})
add_subdirectory(net)

# target
add_executable(client client.cc)
add_executable(server server.cc)
# client for evaluation
add_executable(client_eval client_eval.cc)
add_executable(client_eval_batch client_eval_batch.cc)
add_executable(client_eval_batch_udp client_eval_batch_udp.cc)

# link libraries
target_link_libraries(server PRIVATE net pthread)
target_link_libraries(client PRIVATE nlohmann_json::nlohmann_json net pthread stdc++fs)
target_link_libraries(client_eval PRIVATE nlohmann_json::nlohmann_json net pthread stdc++fs)
target_link_libraries(client_eval_batch PRIVATE nlohmann_json::nlohmann_json net pthread stdc++fs)
target_link_libraries(client_eval_batch_udp PRIVATE nlohmann_json::nlohmann_json net pthread stdc++fs)