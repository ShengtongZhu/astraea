#!/bin/bash

sudo apt-get install ntp ntpdate texlive python-pip python3-pip iperf
sudo apt-get install -y debhelper autotools-dev dh-autoreconf iptables pkg-config iproute2
sudo apt-get install -y makepp libboost-all-dev libprotobuf-dev protobuf-c-compiler protobuf-compiler libjemalloc-dev

# python packages 
python3 -m pip install pip --upgrade
# if you use python3.6
pip3 install protobuf==3.10.0 tensorflow==1.14.0 --upgrade
pip3 install matplotlib==3.2

pip3 install gym sysv_ipc matplotlib numpy pyyaml tabulate

# install cmake manually
wget https://github.com/Kitware/CMake/releases/download/v3.21.1/cmake-3.21.1.tar.gz
tar zvxf cmake-3.21.1.tar.gz && cd cmake-3.21.1
./bootstrap && ./configure
make -j
sudo make install