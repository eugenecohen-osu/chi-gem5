#!/bin/bash

./build/ARM/gem5.opt configs/deprecated/example/se.py --help > m5out/se-help.txt

    # --mem-channels=2 \
    # --mem-type=DDR5_8400_4x8 \
./build/ARM/gem5.opt configs/deprecated/example/se.py --ruby --topology=Pt2Pt \
    --cpu-type=ArmTimingSimpleCPU \
    --num-cpus=4 \
    --num-dirs=2 \
    --num-l3caches=2 \
    --cmd=tests/test-progs/hello/bin/arm/linux/hello
