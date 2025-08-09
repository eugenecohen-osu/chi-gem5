#!/bin/bash

DOCKER_PREFIX=./docker-run

# build the m5term utility
pushd util/term
make
popd

# then build gem5 for RISC-V
NUM_CPUS=$(nproc)
$DOCKER_PREFIX python3 /usr/bin/scons build/RISCV/gem5.opt -j $NUM_CPUS
