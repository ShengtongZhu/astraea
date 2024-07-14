# Find TensorflowCC after it has been built
set(CMAKE_PREFIX_PATH ${CMAKE_BINARY_DIR}/tensorflow_cc_install)
find_package(TensorflowCC REQUIRED)

# boost
find_package(Boost REQUIRED COMPONENTS system filesystem)

file(GLOB LIB_HEADERS ./*.hh)
file(GLOB LIB_SRCS ./*.cc)
add_executable(infer infer.cc ${LIB_HEADERS} ${LIB_SRCS})

# Ensure tensorflow_cc is built before linking
# add_dependencies(infer setup_tensorflow_cc)

# Link the Tensorflow library.
target_link_libraries(infer PRIVATE TensorflowCC::TensorflowCC nlohmann_json::nlohmann_json net pthread stdc++fs ${Boost_LIBRARIES})

# You may also link cuda if it is available.
# find_package(CUDA)
# if(CUDA_FOUND)
#   target_link_libraries(example ${CUDA_LIBRARIES})
# endif()
