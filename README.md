# Astraea

Astraea is a performant DRL-based congestion control for Internet. It optimizes convergence properties, including fairness, responsiveness and stability, while maintaining high throughput, low latency and packet loss rate.

Astraea is built on the top of [Puffer](https://github.com/StanfordSNR/puffer/tree/master)'s network library.

## Build Astraea
### Dependencies
- [mahimahi](https://github.com/ravinet/mahimahi.git) for emulated network environment
- [json](https://github.com/nlohmann/json) for message serialization and deserialization
- [Astraea kernel](`kernel/deb/README.md`)
- [Astraea customized kernel tcp CC module](`kernel/tcp-astraea/README.md`)
- [TensorflowCC](https://github.com/FloopCZ/tensorflow_cc): for Astraea's inference service
- Boost

### Setup customized kernel
Generally speaking, Astraea relies on a customized kernel that supports gathering TCP statistics from the kernel TCP datapath. Please refer to the [kernel](kernel/deb/README.md) for more details.

### Build Astraea kernel TCP CC module
Astraea also uses a customized kernel TCP CC module. Please refer to the [kernel](kernel/tcp-astraea/README.md) for more details.

### Build Astraea client and server 
```bash
cd src
mkdir build && cd build
# if you want to use Astraea's inference service, please add -COMPILE_INFERENCE_SERVICE=ON
cmake ..
make -j
```

### Build Astraea inference service (optional)
> Note: Astraea's batch inference services rely on TensorflowCC, which is a C++ API for TensorFlow. The current version of Astraea's inference server is built on TensorFlow 1.15.0. To compile TensorFlow, you need to install Bazel and other dependencies, for example, cuda and cudnn. Please refer to the official TensorFlow website for more details.
```bash
cd scripts/
./install_tensorflow_cc.sh
```

## Run Astraea
### Run Astraea server
```bash
./src/build/bin/server --port=12345
```

### Run Astraea client with naive python inference helper
```bash
./src/build/bin/client_eval --ip=localhost --port=12345 \
    --cong=cubic \
    --interval=30 \
    --pyhelper=./python/infer.py \
    --model=./models/py/
```

If you want to run Astraea with mahimahi, you can use the following command:
- launch the server similar to the above command
- launch the client with the following command:
```bash
mm-delay 10
# inside the mahimahi shell
./src/build/bin/client_eval --ip=$MAHIMAHI_BASE --port=12345 \
    --cong=cubic \
    --interval=30 \
    --pyhelper=./python/infer.py \
    --model=./models/py/
```


### Run Astraea inference service (optional)
For example, to run Astraea inference service with a pre-trained model with UDP channel:
```bash
./src/build/bin/infer --graph ./models/exported/model.meta --checkpoint ./models/exported/model --batch=0 --channel=udp
```


## Reference
The design, implementation and evaluation of Astraea is detailed in the following paper in EuroSys '24:

```bib
@inproceedings{Astraea,
    author = {Liao, Xudong and Tian, Han and Zeng, Chaoliang and Wan, Xinchen and Chen, Kai},
    title = {Astraea: Towards Fair and Efficient Learning-based Congestion Control},
    year = {2024},
    url = {https://doi.org/10.1145/3627703.3650069},
    booktitle = {Proceedings of the Nineteenth European Conference on Computer Systems},
    series = {EuroSys '24}
}
```

For Astraea's multi-flow environment, please refer to [Astraea-env]().