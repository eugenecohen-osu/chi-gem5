# TODO COPYRIGHT

import argparse
import os
import sys
from pathlib import Path

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

# def update_resource_md5(resource_json_path, resouce_id):
#     disk_image_path = f'resources/{resouce_id}'
#     if os.path.exists(disk_image_path):
#         print(f'updating md5 for {disk_image_path}...')
#         new_md5 = md5_file(Path(disk_image_path))
#         with open(resource_json_path, 'r') as f:
#             extra_json = json.load(f)
#         for i in extra_json:
#             if i['id'] == resouce_id:
#                 print(f'setting {disk_image_path} md5 sum to {new_md5}')
#                 i['md5sum'] = new_md5
#                 with open(resource_json_path, 'w') as f:
#                     json.dump(extra_json, f, indent=4)
#                 break

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

disk_image_name = 'riscv-ubuntu-24.04-img'

# get the original os image if it has not been fetched yet
resource_dir = os.environ["GEM5_RESOURCE_DIR"] or os.path.join(Path.home(), ".cache", "gem5")
disk_image_path = os.path.join(resource_dir, disk_image_name)
if not os.path.exists(disk_image_path):
    print(f'ERROR: image {disk_image_path} not found, downloading it...')
    sys.exit(-1)

workload=obtain_resource("riscv-ubuntu-24.04-boot", clients=['GEM5_RESOURCE_JSON_APPEND'], resource_version="2.0.1")

if args.readfile:
    print(f'setting workload readfile to {args.readfile}')
    workload.set_parameter("readfile", args.readfile)

print(f'restoring checkpoint from {args.checkpoint_path}')
workload.set_parameter("checkpoint", Path(args.checkpoint_path))

board.set_workload(workload)


#simulator = Simulator(board=board, checkpoint_path=args.checkpoint_path)
simulator = Simulator(board=board)
simulator.run()
