#!/bin/bash

mkdir ../_deps
git clone https://github.com/FloopCZ/tensorflow_cc.git ../_deps/tensorflow_cc
cd ../_deps/tensorflow_cc/tensorflow_cc
echo "1.15.0" > PROJECT_VERSION
mkdir -p build
cd build
CURR_DIR=$(pwd)
cmake -DCMAKE_INSTALL_PREFIX=$CURR_DIR -DALLOW_CUDA=OFF ..
make -j
make install
