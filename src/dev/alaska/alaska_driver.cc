/*
 * Copyright (c) 2025 Oregon State University
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "dev/alaska/alaska_driver.hh"
#include "debug/AlaskaDriver.hh"
#include "arch/riscv/handletable.hh"

#include <memory>

#include "base/compiler.hh"
#include "base/logging.hh"
#include "base/trace.hh"
#include "arch/riscv/isa.hh"
#include "cpu/thread_context.hh"

#include "params/AlaskaDriver.hh"
#include "arch/riscv/linux/linux.hh"
#include "sim/process.hh"
#include "sim/se_workload.hh"
#include "sim/syscall_emul_buf.hh"
#include "sim/system.hh"

namespace gem5
{

using namespace RiscvISA;

AlaskaDriver::AlaskaDriver(const Params &p)
    : EmulatedDriver(p), ht0_addr(0), num_ht1_tables(0)
{
    DPRINTF(AlaskaDriver, "Constructing AlaskaDriver\n");
}

void AlaskaDriver::init_ht(ThreadContext *tc, Process *process, Addr mmap_start)
{
    const auto page_size = process->pTable->pageSize();

    // allocate ht0
    ht0_addr = process->seWorkload->allocPhysPages(HT_TABLE_SIZE / page_size);
    tc->setMiscReg(MISCREG_HTBASE, ht0_addr);
    DPRINTF(AlaskaDriver, "Allocated HT0 at %#x\n", ht0_addr);

    // allocate initial HT1 tables
    Addr ht1_addrs[NUM_INITIAL_HT1_TABLES];
    for (int i=0; i<NUM_INITIAL_HT1_TABLES; i++) {
        ht1_addrs[i] = process->seWorkload->allocPhysPages(HT_TABLE_SIZE / page_size);
        DPRINTF(AlaskaDriver, "Allocated HT1-%d at %#x\n", i, ht1_addrs[i]);
    }
    num_ht1_tables = NUM_INITIAL_HT1_TABLES;

    // write the HT1 addresses into HT0 table
    PortProxy &physProxy = process->system->physProxy;
    physProxy.writeBlob(ht0_addr, ht1_addrs, sizeof(ht1_addrs));
    
    // update HTBOUND
    tc->setMiscReg(MISCREG_HTBOUND, num_ht1_tables);

    // map initial HT1 pages into process
    Addr nextVAddr = mmap_start;
    for (int i=0; i<NUM_INITIAL_HT1_TABLES; i++) {
        process->pTable->map(nextVAddr, ht1_addrs[i], HT_TABLE_SIZE, 0);
        nextVAddr += HT_TABLE_SIZE;
    }

}

/**
 * Create an FD entry for the KFD inside of the owning process.
 */
int
AlaskaDriver::open(ThreadContext *tc, int mode, int flags)
{
    DPRINTF(AlaskaDriver, "Opened %s\n", filename);
    auto process = tc->getProcessPtr();
    auto device_fd_entry = std::make_shared<DeviceFDEntry>(this, filename);
    int tgt_fd = process->fds->allocFD(device_fd_entry);
    return tgt_fd;
}

/**
 * Currently, mmap() will simply setup a mapping for the associated
 * device's packet processor's doorbells and creates the event page.
 */
Addr
AlaskaDriver::mmap(ThreadContext *tc, Addr start, uint64_t length,
                       int prot, int tgt_flags, int tgt_fd, off_t offset)
{
    auto process = tc->getProcessPtr();
    auto mem_state = process->memState;

   
    panic_if(!(tgt_flags & RiscvLinux64::TGT_MAP_FIXED),
                     "alaska: only mmap with TGT_MAP_FIXED is supported");


    DPRINTF(AlaskaDriver, "mmap 0x%x length 0x%x\n", start, length);

    // if a previous region was mapped, unmap it
    process->memState->unmapRegion(start, length); // DEBUG: COMMENTED OUT TO REDUCE CONFUSION

    // map in virtual memory to access the HT1 entries, allocating dynamically via
    // the vm_fault as they are accessed
    process->memState->mapRegion(start, length, "handle table", -1, offset, this);

    if (ht0_addr == 0) {
        init_ht(tc, process, start);
    }

    // return the address passed in since this is a TGT_MAP_FIXED mapping
    return start;
}

bool AlaskaDriver::vm_fault(Process *process, Addr vaddr, const VMA *vma)
{
    DPRINTF(AlaskaDriver, "vm_fault %#x\n", vaddr);
    // TODO!
    return false;
}

int
AlaskaDriver::ioctl(ThreadContext *tc, unsigned req, Addr ioc_buf)
{
    switch (req) {
        // case AMDKFD_IOC_GET_CLOCK_COUNTERS:
        //   {
        //     DPRINTF(GPUDriver, "ioctl: AMDKFD_IOC_GET_CLOCK_COUNTERS\n");

        //     TypedBufferArg<kfd_ioctl_get_clock_counters_args> args(ioc_buf);
        //     args.copyIn(virt_proxy);

        //     // Set nanosecond resolution
        //     args->system_clock_freq = 1000000000;

        //     /**
        //      * Derive all clock counters based on the tick. All
        //      * device clocks are identical and perfectly in sync.
        //      */
        //     uint64_t elapsed_nsec = curTick() / sim_clock::as_int::ns;
        //     args->gpu_clock_counter = elapsed_nsec;
        //     args->cpu_clock_counter = elapsed_nsec;
        //     args->system_clock_counter = elapsed_nsec;

        //     args.copyOut(virt_proxy);
        //   }
        //   break;
        default:
          fatal("%s: bad ioctl %d\n", req);
          break;
    }
    return 0;
}

} // namespace gem5
