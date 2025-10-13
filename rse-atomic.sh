#!/bin/bash

./build/RISCV/gem5.opt \
        --outdir out \
        --debug-flags=AlaskaDriver,HandleTableWalker,RiscvHMMU,HTLB,Vma,PageTableWalker,TLB,SyscallBase,SyscallVerbose \
        configs/deprecated/example/se.py \
        --ruby \
        --cpu-type=RiscvHmmuAtomicSimpleCPU \
        --topology=Crossbar \
        --num-cpus=1 \
        --num-dirs=2 \
        --num-l3caches=2 \
        --drivers AlaskaDriver=alaska \
        --cmd=tests/test-progs/min_halloc/bin/riscv/linux/min_halloc
