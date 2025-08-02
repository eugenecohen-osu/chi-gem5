#!/bin/bash

DOCKER_PREFIX=./docker-run

CPU_TYPE=RiscvTimingSimpleCPU
NUM_CORES=1
OUT_DIR=out

mkdir -p $OUT_DIR

$DOCKER_PREFIX ./build/RISCV/gem5.opt --outdir $OUT_DIR configs/deprecated/example/se.py --ruby \
    --cpu-type=$CPU_TYPE \
    --topology=Crossbar \
    --num-cpus=$NUM_CORES \
    --num-dirs=2 \
    --num-l3caches=2 \
    --cmd=tests/test-progs/hello/bin/riscv/linux/hello
