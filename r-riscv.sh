#!/bin/bash
    # --debug-flags=RubyNetwork,SyscallAll \
    # --debug-file=debug.out \
#    -r \
build/RISCV_CHI/gem5.opt \
    tests/gem5/chi_protocol/configs/chi-riscv-thread.py \
    --num-cores 4
