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
    : EmulatedDriver(p), ht0_paddr(0), ht1_vaddr(0), num_ht1_tables(0)
{
    DPRINTF(AlaskaDriver, "Constructing AlaskaDriver\n");
}

void AlaskaDriver::init_ht(ThreadContext *tc, Process *process, Addr mmap_start)
{
    const auto page_size = process->pTable->pageSize();

    // allocate ht0
    ht0_paddr = process->seWorkload->allocPhysPages(HT_TABLE_SIZE / page_size);
    tc->setMiscReg(MISCREG_HTBASE, ht0_paddr);
    DPRINTF(AlaskaDriver, "Allocated HT0 at %#x\n", ht0_paddr);

    // allocate initial HT1 tables and map them to the vaddr passed in the mmap
    ht1_vaddr = mmap_start;
    Addr nextVAddr = ht1_vaddr;
    ht1_paddrs.reserve(NUM_INITIAL_HT1_TABLES);
    for (int i=0; i<NUM_INITIAL_HT1_TABLES; i++) {
        alloc_ht1(process, i, nextVAddr);
        nextVAddr += HT_TABLE_SIZE;
    }
}

void AlaskaDriver::alloc_ht1(Process *process, uint64_t index, Addr vaddr)
{
    const auto page_size = process->pTable->pageSize();

    if (index >= num_ht1_tables) {
        num_ht1_tables = index+1;
        ht1_paddrs.resize(num_ht1_tables); // grow ht1_paddrs vector, zeroing new entries
    }

    auto phys_addr = process->seWorkload->allocPhysPages(HT_TABLE_SIZE / page_size);
    ht1_paddrs[index] = phys_addr;
    DPRINTF(AlaskaDriver, "Allocated HT1-%d at %#x\n", index, phys_addr);

    // write the HT1 address into HT0 table
    PortProxy &physProxy = process->system->physProxy;
    Addr ht0_entry_addr = ht0_paddr + index * sizeof(HT_Entry);
    physProxy.writeBlob(ht0_entry_addr, &ht1_paddrs[index], sizeof(ht1_paddrs[index]));

    for (auto id: process->contextIds) {
        ThreadContext *tc = process->system->threads[id];
        // set new HTBOUND and invalidate HTLB
        tc->setMiscReg(MISCREG_HTBOUND, num_ht1_tables * HT_TABLE_MAX_ENTRIES);
        tc->setMiscReg(MISCREG_HTINVAL, 0);
    }

    // map the ht1 pages into process
    DPRINTF(AlaskaDriver, "Mapping HT1-%d vaddr %#x -> %#x\n", index, vaddr, phys_addr);
    process->pTable->map(vaddr, ht1_paddrs[index], HT_TABLE_SIZE, 0);
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
    process->memState->unmapRegion(start, length);

    if (length > HT_MAX_HT_SIZE) {
        DPRINTF(AlaskaDriver, "mmap length exceeds max HT size");
        return 0;
    }
    ht1_mmap_length = length;

    // map in virtual memory to access the HT1 entries, allocating dynamically via
    // the vm_fault as they are accessed
   
    process->memState->mapRegion(start, ht1_mmap_length, "handle table", -1, offset, this);

    if (ht0_paddr == 0) {
        init_ht(tc, process, start);
    }

    // return the address passed in since this is a TGT_MAP_FIXED mapping
    return start;
}

bool AlaskaDriver::vm_fault(Process *process, Addr vaddr, const VMA *vma)
{
    DPRINTF(AlaskaDriver, "vm_fault %#x\n", vaddr);
    
    if ((vaddr < ht1_vaddr) || (vaddr >= ht1_vaddr + ht1_mmap_length)) {
        return false;
    }

    // calculate the desired ht1 index from the vaddr
    Addr ht1_offset = vaddr - ht1_vaddr;
    uint64_t ht1_fault_index = ht1_offset >> HT_TABLE_SIZE_BITS;

    // if we got a vmfault we should be hitting an HT1 that is not yet allocated
    assert(num_ht1_tables < (ht1_fault_index+1));

    // keep allocating HT1s until we reach this new index
    for(uint64_t ht1_idx=num_ht1_tables; ht1_idx<=ht1_fault_index; ht1_idx++) {
        Addr vaddr = ht1_vaddr + ht1_idx*HT_TABLE_SIZE;
        alloc_ht1(process, ht1_idx, vaddr);        
    }

    // fault handled
    return true;
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
