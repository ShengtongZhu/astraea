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

# include directory
include_directories(./net ${JSON_DIR}/single_include/nlohmann ${CMAKE_INCLUDE_OUTPUT_DIRECTORY})
add_subdirectory(net)

# batch inference service
if(COMPILE_INFERENCE_SERVICE)
    add_subdirectory(inference)
endif()

# target
add_executable(client client.cc)
add_executable(server server.cc)
# client for evaluation
add_executable(client_eval client_eval.cc)
# NEW: client receiver and server sender
add_executable(client_receiver client_receiver.cc)
add_executable(server_sender server_sender.cc)
# NEW: updated client receiver and server sender with size request protocol
add_executable(new_client_receiver new_client_receiver.cc)
add_executable(new_server_sender new_server_sender.cc)
# passive client
add_executable(passive_client passive_client.cc)
# client for batch inference evaluation
if(COMPILE_INFERENCE_SERVICE)
    add_executable(client_eval_batch client_eval_batch.cc)
    add_executable(client_eval_batch_udp client_eval_batch_udp.cc)
endif()

# NEW: no-communication size argument variants
add_executable(new_client_receiver_nocomm new_client_receiver_nocomm.cc)
add_executable(new_server_sender_nocomm new_server_sender_nocomm.cc)
endif()

# link libraries
target_link_libraries(server PRIVATE net pthread)
target_link_libraries(client PRIVATE nlohmann_json::nlohmann_json net pthread stdc++fs)
target_link_libraries(client_eval PRIVATE nlohmann_json::nlohmann_json net pthread stdc++fs)
# NEW: link libraries for new executables
target_link_libraries(client_receiver PRIVATE nlohmann_json::nlohmann_json net pthread stdc++fs)
target_link_libraries(server_sender PRIVATE nlohmann_json::nlohmann_json net pthread stdc++fs)
# NEW: link libraries for updated executables with size request protocol
target_link_libraries(new_client_receiver PRIVATE nlohmann_json::nlohmann_json net pthread stdc++fs)
target_link_libraries(new_server_sender PRIVATE nlohmann_json::nlohmann_json net pthread stdc++fs)
target_link_libraries(passive_client PRIVATE nlohmann_json::nlohmann_json net pthread stdc++fs)
# NEW: link libraries for no-communication size argument variants
target_link_libraries(new_client_receiver_nocomm PRIVATE nlohmann_json::nlohmann_json net pthread stdc++fs)
target_link_libraries(new_server_sender_nocomm PRIVATE nlohmann_json::nlohmann_json net pthread stdc++fs)
if(COMPILE_INFERENCE_SERVICE)
    target_link_libraries(client_eval_batch PRIVATE nlohmann_json::nlohmann_json net pthread stdc++fs)
    target_link_libraries(client_eval_batch_udp PRIVATE nlohmann_json::nlohmann_json net pthread stdc++fs)
endif()