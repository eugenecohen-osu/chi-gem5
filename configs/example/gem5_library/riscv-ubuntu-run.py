# Copyright (c) 2021-2025 The Regents of the University of California
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met: redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer;
# redistributions in binary form must reproduce the above copyright
# notice, this list of conditions and the following disclaimer in the
# documentation and/or other materials provided with the distribution;
# neither the name of the copyright holders nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

"""
This script shows an example of running a full system RISCV Ubuntu boot
simulation using the gem5 library. This simulation boots Ubuntu 24.04 using
2 TIMING CPU cores. The simulation ends when the startup is completed
successfully.

Usage
-----

```
scons build/ALL/gem5.opt
./build/ALL/gem5.opt configs/example/gem5_library/riscv-ubuntu-run.py
```
"""

import argparse
import os
from pathlib import Path
import subprocess
import glob
import json

from gem5.components.boards.riscv_board import RiscvBoard

# With RISCV, we use simple caches.
from gem5.components.cachehierarchies.classic.private_l1_private_l2_walk_cache_hierarchy import (
    PrivateL1PrivateL2WalkCacheHierarchy,
)
from gem5.components.memory import DualChannelDDR4_2400
from gem5.components.processors.cpu_types import CPUTypes
from gem5.components.processors.cpu_types import (
    get_cpu_type_from_str,
    get_cpu_types_str_set,
)
from gem5.components.processors.simple_processor import SimpleProcessor
from gem5.isas import ISA
from gem5.resources.resource import obtain_resource
from gem5.simulate.exit_handler import (
    ExitHandler,
    KernelBootedExitHandler,
)
from gem5.simulate.simulator import Simulator
from gem5.utils.override import overrides

from gem5.resources.md5_utils import md5_file

def update_resource_md5(resource_json_path, resouce_id):
    disk_image_path = f'resources/{resouce_id}'
    if os.path.exists(disk_image_path):
        print(f'updating md5 for {disk_image_path}...')
        new_md5 = md5_file(Path(disk_image_path))
        with open(resource_json_path, 'r') as f:
            extra_json = json.load(f)
        for i in extra_json:
            if i['id'] == resouce_id:
                print(f'setting {disk_image_path} md5 sum to {new_md5}')
                i['md5sum'] = new_md5
                with open(resource_json_path, 'w') as f:
                    json.dump(extra_json, f, indent=4)
                break

parser = argparse.ArgumentParser(
    description="A gem5 script for testing RISC-V instructions"
)

parser.add_argument(
    "--pydebug",  action="store_true", help="Enable python remote attach debugging"
)

parser.add_argument(
    "--readfile", type=str, required=False, help="The file to execute on boot"
)

parser.add_argument(
    "--checkpoint-path",
    type=str,
    required=False,
    default="riscv-ubuntu-checkpoint/",
    help="The directory to store the checkpoint.",
)

parser.add_argument(
    "--cpu", type=str, default=CPUTypes.TIMING, choices=get_cpu_types_str_set(), help="The CPU type used."
)

parser.add_argument(
    "-n",
    "--num-cores",
    type=int,
    default=1,
    required=False,
    help="The number of CPU cores to run.",
)

args = parser.parse_args()

if args.pydebug:
    print(f'starting python debug server')
    import debugpy
    print(f'configuring python3 path')
    debugpy.configure(python="/usr/bin/python3")
    print('starting debugpy listen')
    debugpy.listen(5678)
    print("Waiting for debugger attach on port 5678...")
    debugpy.wait_for_client()
    print('DEBUGGER ATTACHED')

# Here we setup the parameters of the l1 and l2 caches.
cache_hierarchy = PrivateL1PrivateL2WalkCacheHierarchy(
    l1d_size="16KiB", l1i_size="16KiB", l2_size="256KiB"
)

# Memory: Dual Channel DDR4 2400 DRAM device.

memory = DualChannelDDR4_2400(size="3GiB")

# Here we setup the processor. We use a simple processor.
cpu=get_cpu_type_from_str(args.cpu)
print(f'using cpu type {cpu}')
processor = SimpleProcessor(
    cpu_type=cpu,
    isa=ISA.RISCV,
    num_cores=args.num_cores
)

# Here we setup the board. The RiscvBoard allows for Full-System RISCV
# simulations.
board = RiscvBoard(
    clk_freq="3GHz",
    processor=processor,
    memory=memory,
    cache_hierarchy=cache_hierarchy,
)

kernel_args = board.get_default_kernel_args()
#print("override init to /bin/bash")
#kernel_args.append("init=/bin/bash")

# Here we a full system workload: "riscv-ubuntu-24.04-boot" which boots
# Ubuntu 24.04. Once the system successfully boots it encounters an
# `gem5-bridge hypercall 3` command which stops the simulation. When the
# simulation has ended you may inspect `m5out/board.platform.terminal` to see
# the simulated system's stdout.

disk_image_name = 'riscv-ubuntu-24.04-img'

# get the original os image if it has not been fetched yet
resource_dir = os.environ["GEM5_RESOURCE_DIR"] or os.path.join(Path.home(), ".cache", "gem5")
disk_image_path = os.path.join(resource_dir, disk_image_name)
if not os.path.exists(disk_image_path):
    print(f'image {disk_image_path} not found, downloading it...')
    # just get the vanilla os image first
    orig_res = obtain_resource(disk_image_name, clients=['gem5-resources'], resource_version="2.0.0")
    local_path = orig_res.get_local_path()
    print(f'image downloaded to {local_path}')


files_to_copy = [
    '*.ko',
    '*.mod',
    'Module.symvers',
    'modules.order',
    'min_halloc'
]

# now copy in files from ../kmod
gem5_dir = Path(__file__).resolve().parents[3]
gem5img_path = os.path.join(gem5_dir, 'util', 'gem5img.py')
src_dir = os.path.join(gem5_dir.parent, 'kmod')
mount_dir = os.path.join(gem5_dir, 'mnt')
dst_dir = os.path.join(mount_dir, 'yukon')
os.makedirs(mount_dir, exist_ok=True)
print(f'mounting {disk_image_path} to {mount_dir}')
subprocess.run(["python3", gem5img_path, "mount", disk_image_path, mount_dir])
# make sure the mount succeeded
if not os.path.exists(os.path.join(mount_dir, 'usr')):
    raise Exception(f'could not find usr at {mount_dir}, perhaps mount failed')
# copy in files
copy_excpetion=None
try:
    subprocess.check_call(['sudo', '/bin/mkdir', '-p', dst_dir])
    for entry in files_to_copy:
        entry_path=os.path.join(src_dir, entry)
        file_list = glob.glob(entry_path, root_dir=src_dir)
        for file in file_list:
            subprocess.check_call(['sudo', '/bin/cp', '-v', f'{file}', dst_dir])
    os.sync
except Exception as e:
    print(f'got an error, unmounting first...')
    copy_excpetion = e
print(f'ummounting {mount_dir}')
subprocess.run(["python3", gem5img_path, "umount", mount_dir], check=True)
if copy_excpetion:
    raise copy_excpetion


# update the md5 in resource-extra.json to always match the image file
if "GEM5_RESOURCE_JSON_APPEND" in os.environ:
    json_extra_path = os.environ["GEM5_RESOURCE_JSON_APPEND"]
    update_resource_md5(json_extra_path, disk_image_name)

workload=obtain_resource("riscv-ubuntu-24.04-boot", clients=['GEM5_RESOURCE_JSON_APPEND'], resource_version="2.0.1")

kernel_args = board.get_default_kernel_args()

# force boot to bash to make things faster (TODO: make python argument)
print("override init to /bin/bash")
kernel_args.append("init=/bin/bash")
workload.set_parameter("kernel_args", kernel_args)
if args.readfile:
    print(f'setting workload readfile to {args.readfile}')
    workload.set_parameter("readfile", args.readfile)

board.set_workload(workload)

print(f'workload is {workload} kernel_args is {kernel_args} disk_device is {board.get_disk_device()}')

# Examples of how you can override the default exit handler behaviors.
# Exit handlers don't have to be specified in the config script if you don't
# want to modify/override their default behaviors.


# You can inherit from either the class that handles a certain hypercall by
# default, or inherit directly from ExitHandler and specify a hypercall number.
# See src/python/gem5/simulate/exit_handler.py for more information on which
# behaviors map to which hypercalls, and what the default behaviors are.
# class CustomKernelBootedExitHandler(KernelBootedExitHandler):
#     @overrides(KernelBootedExitHandler)
#     def _process(self, simulator: "Simulator") -> None:
#         print("First exit: kernel booted")

#     @overrides(KernelBootedExitHandler)
#     def _exit_simulation(self) -> bool:
#         return False


# class CustomAfterBootExitHandler(ExitHandler, hypercall_num=2):
#     @overrides(ExitHandler)
#     def _process(self, simulator: "Simulator") -> None:
#         print("Second exit: Started `after_boot.sh` script")

#     @overrides(ExitHandler)
#     def _exit_simulation(self) -> bool:
#         return False


# class AfterBootScriptExitHandler(ExitHandler, hypercall_num=3):
#     @overrides(ExitHandler)
#     def _process(self, simulator: "Simulator") -> None:
#         print(f"Third exit: {self.get_handler_description()}")

#     @overrides(ExitHandler)
#     def _exit_simulation(self) -> bool:
#         return True


simulator = Simulator(board=board)
simulator.run()
print(
    "Exiting @ tick {} because {}.".format(
        simulator.get_current_tick(), simulator.get_last_exit_event_cause()
    )
)

print("Taking a checkpoint at", args.checkpoint_path)
simulator.save_checkpoint(args.checkpoint_path)
print("Done taking a checkpoint")
