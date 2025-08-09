#!/bin/bash

DOCKER_PREFIX=./docker-run

THISDIR=$(pwd -P)
export GEM5_RESOURCE_DIR=$THISDIR/resources
mkdir -p $GEM5_RESOURCE_DIR

build/RISCV/gem5.opt --debug-help
