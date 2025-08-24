// add copyright

#include "dev/alaska/alaska_driver.hh"
#include "debug/AlaskaDriver.hh"
// #include "dev/hsa/hsa_packet_processor.hh"
// #include "dev/hsa/kfd_event_defines.h"
// #include "dev/hsa/kfd_ioctl.h"

#include <memory>

//#include "arch/x86/page_size.hh"
#include "base/compiler.hh"
#include "base/logging.hh"
#include "base/trace.hh"
#include "cpu/thread_context.hh"

// #include "mem/port_proxy.hh"
// #include "mem/se_translating_port_proxy.hh"
// #include "mem/translating_port_proxy.hh"
#include "params/AlaskaDriver.hh"
#include "arch/riscv/linux/linux.hh"
#include "sim/process.hh"
#include "sim/se_workload.hh"
#include "sim/syscall_emul_buf.hh"

namespace gem5
{

AlaskaDriver::AlaskaDriver(const Params &p)
    : EmulatedDriver(p)
{
    DPRINTF(AlaskaDriver, "Constructing AlaskaDriver\n");
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

    // map in regular memory to service as the handle table
    process->memState->mapRegion(start, length, "handle table", -1, offset);

    // return the address passed in since this is a TGT_MAP_FIXED mapping
    return start;
}

int
AlaskaDriver::ioctl(ThreadContext *tc, unsigned req, Addr ioc_buf)
{
    // TranslatingPortProxy fs_proxy(tc);
    // SETranslatingPortProxy se_proxy(tc);
    // PortProxy &virt_proxy = FullSystem ? fs_proxy : se_proxy;
    // auto process = tc->getProcessPtr();
    // auto mem_state = process->memState;

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

// void
// AlaskaDriver::setMtype(RequestPtr req)
// {
//     // If we are a dGPU then set the MTYPE from our VMAs.
//     if (isdGPU) {
//         assert(!FullSystem);
//         AddrRange range = RangeSize(req->getVaddr(), req->getSize());
//         auto vma = gpuVmas.contains(range);
//         assert(vma != gpuVmas.end());
//         DPRINTF(GPUShader, "Setting req from [%p - %p] MTYPE %d\n"
//                 "%d\n", range.start(), range.end(), vma->second);
//         req->setCacheCoherenceFlags(vma->second);
//     // APUs always get the default MTYPE
//     } else {
//         req->setCacheCoherenceFlags(defaultMtype);
//     }
// }

} // namespace gem5
