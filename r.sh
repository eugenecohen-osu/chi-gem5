#!/bin/bash

DOCKER_PREFIX=./docker-run

THISDIR=$(pwd -P)
export GEM5_RESOURCE_DIR=$THISDIR/resources
mkdir -p $GEM5_RESOURCE_DIR

# build/RISCV/gem5.opt --help > help.txt
# build/RISCV/gem5.opt --debug-help > debug-help.txt


# build/RISCV/gem5.opt \
#     -r \
#     --debug-flags=O3PipeView \
#     --debug-file=debug.out \
#     configs/learning_gem5/part1/simple-riscv.py

# build/RISCV/gem5.opt \
#     -r \
#     --debug-flags=ExecAll,O3CPUAll \
#     configs/learning_gem5/part1/simple-riscv.py

# build/RISCV/gem5.opt \
#     -r \
#     --debug-flags=ExecAll \
#     --debug-file=debug.out \
#     configs/learning_gem5/part1/simple-riscv.py

# build/RISCV/gem5.opt \
#     configs/example/gem5_library/riscv-ubuntu-run.py

# build/RISCV/gem5.opt \
#     configs/example/gem5_library/riscv-fs.py

build/RISCV/gem5.opt \
  configs/example/riscv/fs_linux.py \
--caches --l1i_size=16kB --l1d_size=16kB \
--l2cache --l2_size=256kB \
--mem-type=DDR4_2400_8x8 \
--mem-size=3GB \
--cpu-type=TimingSimpleCPU \
--kernel=img/bootloader-vmlinux-5.10 \
--disk-image=img/ubuntu-24.04.2-preinstalled-server-riscv64+unmatched.img \
--command-line="console=ttyS0"


#--command-line="console=ttyS0 root=/dev/vda1 ro"
