#!/bin/bash

THISDIR=$(realpath $(pwd))
export GEM5_RESOURCE_DIR=$THISDIR/resources
mkdir -p $GEM5_RESOURCE_DIR

# with help from https://g.co/gemini/share/bed2973c834a
function wait_for_gem5_console() {
  local host="$1"
  local port="$2"
  local timeout="$3"
  local start_time=$(date +%s)
  
  echo "Waiting $timeout seconds for $host:$port to be available..."

  # Loop until the port is open or the timeout is reached.
  while ! nc -z -w 1 "$host" "$port" >/dev/null 2>&1; do
    # Check if the elapsed time exceeds the timeout.
    local current_time=$(date +%s)
    local elapsed_time=$((current_time - start_time))
    if [ "$elapsed_time" -ge "$timeout" ]; then
      echo "Error: $host:$port is not available after $timeout seconds."
      return 1
    fi

    # Wait for a second before the next attempt.
    sleep 1
  done

  echo "$host:$port is now available."
  sleep 1
  nc $host $port

  return 0
}


# build/RISCV/gem5.opt --help > help.txt
# build/RISCV/gem5.opt --debug-help > debug-help.txt


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
#     configs/example/gem5_library/riscv-ubuntu-run.py

# build/RISCV/gem5.opt \
#     configs/example/gem5_library/riscv-fs.py

#--cpu-type=TimingSimpleCPU \

# build/RISCV/gem5.opt \
#   configs/example/riscv/fs_linux.py \
# --caches --l1i_size=16kB --l1d_size=16kB \
# --l2cache --l2_size=256kB \
# --mem-type=DDR4_2400_8x8 \
# --mem-size=3GB \
# --cpu-type=RiscvHmmuTimingSimpleCPU \
# --kernel=img/bootloader-vmlinux-5.10 \
# --disk-image=img/ubuntu-24.04.2-preinstalled-server-riscv64+unmatched.img \
# --command-line="console=ttyS0"
#--command-line="console=ttyS0 root=/dev/vda1 ro"

wait_for_gem5_console localhost 3456 60 &

build/RISCV/gem5.opt \
  configs/example/gem5_library/riscv-ubuntu-run.py

# --caches --l1i_size=16kB --l1d_size=16kB \
# --l2cache --l2_size=256kB \
# --mem-type=DDR4_2400_8x8 \
# --mem-size=3GB \
# --cpu-type=RiscvHmmuTimingSimpleCPU \
# --kernel=img/bootloader-vmlinux-5.10 \
# --disk-image=img/ubuntu-24.04.2-preinstalled-server-riscv64+unmatched.img \
# --command-line="console=ttyS0"





