# Astraea

Astraea is a performant DRL-based congestion control for Internet. It optimizes convergence properties, including fairness, responsiveness and stability, while maintaining high throughput, low latency and packet loss rate.

Astraea is built on the top of [Puffer]()'s network library.

## Build Astraea sender and receiver
### Dependencies
- [json]()
- [TensorflowCC]()
- [Astraea kernel patch]()
- [Astraea customized kernel tcp CC module]()
- Boost

### Setup customized kernel
1. Apply Astraea kernel patch to the kernel source code.
2. Compile the kernel and install it.

### Build Astraea kernel TCP CC module

### Build Astraea sender and receiver 

> Note: Astraea's batch inference services rely on TensorflowCC, which is a C++ API for TensorFlow. The current version of Astraea's inference server is built on TensorFlow 1.15.0. To compile TensorFlow, you need to install Bazel and other dependencies, for example, cuda and cudnn. Please refer to the official TensorFlow website for more details.


## Run Astraea


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