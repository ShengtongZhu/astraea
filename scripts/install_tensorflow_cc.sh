#!/bin/bash

# install bazel if not installed
if ! [ -x "$(command -v bazel)" ]; then
    wget https://github.com/bazelbuild/bazel/releases/download/0.26.1/bazel-0.26.1-installer-linux-x86_64.sh
    bash bazel-0.26.1-installer-linux-x86_64.sh --user
    export PATH="$HOME/bin:$PATH"
fi

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
