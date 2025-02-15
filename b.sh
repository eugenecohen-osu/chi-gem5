#!/bin/bash

# build the m5term utility
pushd util/term
make
popd

# then build gem5 for RISC-V
python3 `which scons` build/RISCV/gem5.opt -j `nproc`
