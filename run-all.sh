#!/bin/bash

DOCKER_PREFIX=./docker-run

function run_thrash_mesh {
    CPU_TYPE=$1
    NUM_CORES=$2
    THRASH_STRIDE=$3
    OUT_DIR=$4

    rm -rf $OUT_DIR
    $DOCKER_PREFIX ./build/RISCV_CHI/gem5.opt --outdir $OUT_DIR configs/deprecated/example/se.py --ruby \
        --cpu-type=$CPU_TYPE \
        --topology=CustomMesh \
        --chi-config=configs/example/noc_config/2x4.py \
        --num-cpus=$NUM_CORES \
        --num-dirs=2 \
        --num-l3caches=2 \
        --cmd=tests/test-progs/thrasher/bin/riscv/linux/thrasher \
        --options $THRASH_STRIDE

}

function run_thrash_xbar {
    CPU_TYPE=$1
    NUM_CORES=$2
    THRASH_STRIDE=$3
    OUT_DIR=$4

    rm -rf $OUT_DIR
    $DOCKER_PREFIX ./build/RISCV_CHI/gem5.opt --outdir $OUT_DIR configs/deprecated/example/se.py --ruby \
        --cpu-type=$CPU_TYPE \
        --topology=Pt2Pt \
        --num-cpus=$NUM_CORES \
        --num-dirs=2 \
        --num-l3caches=2 \
        --cmd=tests/test-progs/thrasher/bin/riscv/linux/thrasher \
        --options $THRASH_STRIDE

}

run_thrash_mesh RiscvTimingSimpleCPU 1 1 out_1c_stride1
run_thrash_mesh RiscvTimingSimpleCPU 2 1 out_2c_stride1
run_thrash_mesh RiscvTimingSimpleCPU 4 1 out_4c_stride1
run_thrash_mesh RiscvTimingSimpleCPU 8 1 out_8c_stride1

run_thrash_mesh RiscvTimingSimpleCPU 1 16 out_1c_stride16
run_thrash_mesh RiscvTimingSimpleCPU 2 16 out_2c_stride16
run_thrash_mesh RiscvTimingSimpleCPU 4 16 out_4c_stride16
run_thrash_mesh RiscvTimingSimpleCPU 8 16 out_8c_stride16

./scrape-stats.py --out png
