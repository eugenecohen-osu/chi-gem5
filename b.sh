#!/bin/bash

# make sure to execute this under docker, i.e.:
#  ./docker-run ./b.sh

# build the m5term utility
pushd util/term
make
popd

# then build gem5 for RISC-V
python3 `which scons` build/RISCV/gem5.opt -j `nproc`
