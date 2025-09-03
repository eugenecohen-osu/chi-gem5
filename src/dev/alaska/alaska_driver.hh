#ifndef __ALASKA_DRIVER_HH__
#define __ALASKA_DRIVER_HH__

#include <cassert>
#include <cstdint>
//#include <set>
//#include <unordered_map>

#include "base/addr_range_map.hh"
#include "base/types.hh"
//#include "enums/GfxVersion.hh"
#include "mem/request.hh"
#include "sim/emul_driver.hh"

namespace gem5
{

struct AlaskaDriverParams;

class ThreadContext;

class AlaskaDriver final : public EmulatedDriver
{
    static constexpr uint32_t NUM_INITIAL_HT1_TABLES = 2;

  public:
    typedef AlaskaDriverParams Params;
    AlaskaDriver(const Params &p);
    int ioctl(ThreadContext *tc, unsigned req, Addr ioc_buf) override;

    int open(ThreadContext *tc, int mode, int flags) override;
    Addr mmap(ThreadContext *tc, Addr start, uint64_t length,
              int prot, int tgt_flags, int tgt_fd, off_t offset) override;
    bool vm_fault(Process *process, Addr vaddr, const VMA *vma) override;

  protected:

    void init_ht(ThreadContext *tc, Process *process, Addr mmap_start);

  private:
    
    Addr ht0_addr;
    uint64_t num_ht1_tables;

};

}

#endif
