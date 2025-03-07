#!/bin/bash

# build the m5term utility
pushd util/term
make
popd

# then build gem5 for ARM
SCONS=$(which scons)
python3 $SCONS --verbose build/ARM/gem5.opt -j `nproc`
