# Astraea customized kernel
This repository contains the Astraea customized kernel, built on top of [Orca](https://github.com/Soheil-ab/Orca).

> This kernel is compiled with Linux kernel source code with version 5.4.73 and includes our custom patch. The patch can be found at `../patch/linux-5-4.patch`.

## Install the kernel
```bash
sudo apt install ./linux-headers-5.4.73-learner_5.4.73-learner-1_amd64.deb
sudo apt install ./linux-image-5.4.73-learner_5.4.73-learner-1_amd64.deb
```

You can also use our patch to build the kernel from scratch.
