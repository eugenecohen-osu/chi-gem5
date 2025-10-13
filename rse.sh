#!/bin/bash

#DOCKER_PREFIX=./docker-run

# CPU_TYPE=RiscvHmmuTimingSimpleCPU
# NUM_CORES=1
# OUT_DIR=out

# mkdir -p $OUT_DIR

#./build/RISCV/gem5.opt --outdir out configs/deprecated/example/se.py --list-cpu-types

exit
./build/RISCV/gem5.opt \
        --outdir out \
        --debug-flags=AlaskaDriver,HandleTableWalker,RiscvHMMU,HTLB,HTLBVerbose,Vma,PageTableWalker,TLB,SyscallBase,SyscallVerbose \
        configs/deprecated/example/se.py \
        --ruby \
        --cpu-type=RiscvHmmuTimingSimpleCPU \
        --topology=Crossbar \
        --num-cpus=1 \
        --num-dirs=2 \
        --num-l3caches=2 \
        --drivers AlaskaDriver=alaska \
        --cmd=tests/test-progs/min_halloc/bin/riscv/linux/min_halloc

# $DOCKER_PREFIX ./build/RISCV/gem5.opt --outdir $OUT_DIR configs/deprecated/example/se.py --ruby \
#     --cpu-type=$CPU_TYPE \
#     --topology=Crossbar \
#     --num-cpus=$NUM_CORES \
#     --num-dirs=2 \
#     --num-l3caches=2 \
#     --drivers AlaskaDriver=alaska \
#     --cmd=tests/test-progs/min_halloc/bin/riscv/linux/min_halloc

