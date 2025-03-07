#!/bin/bash


#build/ARM/gem5.opt --help > help.txt
#build/ARM/gem5.opt --debug-help > debug-help.txt

#    -r \
build/ARM/gem5.opt \
    --debug-flags=RubyNetwork \
    --debug-file=debug.out \
    tests/gem5/chi_protocol/configs/chi-arm-thread.py \
    --num-cores 2

#ExecAll,AnnotateAll,CacheAll,Arm,RubyCHIGenericVerbose,AMBA,Ruby
# build/ARM/gem5.opt \
#     -r \
#     --debug-flags=RubyNetwork \
#     --debug-file=debug.out \
#     tests/gem5/chi_protocol/configs/chi-with-isa.py \
#     --num-cores 2 \
#     arm


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
#     -r \
#     --debug-flags=ExecAll \
#      --debug-file=debug.out \
#     configs/example/gem5_library/riscv-ubuntu-run.py
