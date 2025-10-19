#!/bin/bash

set -e
set -u

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
  #nc $host $port
  echo UART CONSOLE: to connect to console do: telnet $host $port
  #telnet $host $port

  return 0
}

wait_for_gem5_console localhost 3456 60 &


#  --pydebug \
export M5_OVERRIDE_PY_SOURCE=true
export GEM5_RESOURCE_JSON_APPEND=./resource-extra.json
echo M5_OVERRIDE_PY_SOURCE is $M5_OVERRIDE_PY_SOURCE
  build/RISCV/gem5.opt \
    configs/example/gem5_library/riscv-ubuntu-resume.py \
    --cpu atomic \
    --checkpoint-path $THISDIR/yukon


