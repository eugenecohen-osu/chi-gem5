/*
 * Copyright (c) 2001-2005 The Regents of The University of Michigan
 * Copyright (c) 2007 MIPS Technologies, Inc.
 * Copyright (c) 2020 Barkhausen Institut
 * Copyright (c) 2021 Huawei International
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

#ifndef __ARCH_RISCV_HTLB_HH__
#define __ARCH_RISCV_HTLB_HH__

#include <list>

#include "arch/riscv/tlb.hh"
#include "arch/riscv/handletable.hh"
#include "params/RiscvHTLB.hh"

namespace gem5
{

class ThreadContext;

/* To maintain compatibility with other architectures, we'll
   simply create an ITLB and DTLB that will point to the real TLB */
namespace RiscvISA {

// class MemAccessInfo
// {
//   public:
//     PrivilegeMode priv;
//     bool virt;
//     bool force_virt;
//     bool hlvx;
//     bool lr;

//     MemAccessInfo() = default;
//     MemAccessInfo(
//       PrivilegeMode priv, bool virt, bool force_virt, bool hlvx, bool lr) :
//       priv(priv), virt(virt), force_virt(force_virt), hlvx(hlvx), lr(lr) {}

//     bool
//     bypassTLB() const
//     {
//         return (force_virt || hlvx);
//     }
// };

class HandleWalker;

class HTLB : public BaseTLB
{
    typedef std::list<HTlbEntry *> EntryList;

  protected:
    size_t size;
    std::vector<HTlbEntry> tlb; // our TLB
    HTlbEntryTrie trie;          // for quick access
    EntryList freeList;         // free entries
    uint64_t lruSeq;

    HandleWalker *walker;

    struct TlbStats : public statistics::Group
    {
        TlbStats(statistics::Group *parent);

        statistics::Scalar readHits;
        statistics::Scalar readMisses;
        statistics::Scalar readAccesses;
        statistics::Scalar writeHits;
        statistics::Scalar writeMisses;
        statistics::Scalar writeAccesses;

        statistics::Formula hits;
        statistics::Formula misses;
        statistics::Formula accesses;
    } stats;

  public:
    BasePMAChecker *pma;
    PMP *pmp;

  public:
    typedef RiscvHTLBParams Params;
    HTLB(const Params &p);

    HandleWalker *getWalker();

    void takeOverFrom(BaseTLB *old) override {}

    /**
     * Insert an entry into the TLB.
     * @param handle_id The handle id number.
     * @param entry The entry to insert.
     */
    HTlbEntry *insert(Addr handle_id, const HTlbEntry &entry);
    void flushAll() override;
    void demapPage(Addr vaddr, uint64_t asn);

    // Fault checkPermissions(ThreadContext* tc, MemAccessInfo mem_access,
    //                         Addr vaddr, BaseMMU::Mode mode, PTESv39 pte,
    //                         Addr gvaddr = 0x0,
    //                         XlateStage stage = XlateStage::FIRST_STAGE);

    Fault createHandlefault(Addr vaddr, BaseMMU::Mode mode, Addr gpaddr = 0x0,
                            bool virt = false);

    MemAccessInfo getMemAccessInfo(ThreadContext *tc, BaseMMU::Mode mode,
                                  const Request::ArchFlagsType arch_flags);

    // Checkpointing
    void serialize(CheckpointOut &cp) const override;
    void unserialize(CheckpointIn &cp) override;

    /**
     * Get the table walker port. This is used for
     * migrating port connections during a CPU takeOverFrom()
     * call. For architectures that do not have a table walker,
     * NULL is returned, hence the use of a pointer rather than a
     * reference. For RISC-V this method will always return a valid
     * port pointer.
     *
     * @return A pointer to the walker port
     */
    Port *getTableWalkerPort() override;

    Addr hiddenTranslateWithTLB(Addr vhaddr, uint16_t asid, BaseMMU::Mode mode);

    Fault translateAtomic(const RequestPtr &req,
                          ThreadContext *tc, BaseMMU::Mode mode) override;
    void translateTiming(const RequestPtr &req, ThreadContext *tc,
                         BaseMMU::Translation *translation,
                         BaseMMU::Mode mode) override;
    Fault translateFunctional(const RequestPtr &req, ThreadContext *tc,
                              BaseMMU::Mode mode) override;
    Fault finalizePhysical(const RequestPtr &req, ThreadContext *tc,
                           BaseMMU::Mode mode) const override;

    Addr
    getValidAddr(Addr vaddr, ThreadContext *tc, BaseMMU::Mode mode)
    {
        return vaddr;
    }
    /**
     * Perform the tlb lookup
     * @param handle_id The handle id number.
     * @param asid The address space identifier as specified by satp.
     * @param mode The mode of the memory operation.
     * @param hidden If the lookup should be hidden from the statistics.
     */
    HTlbEntry *lookup(Addr handle_id, uint16_t asid, BaseMMU::Mode mode, bool hidden);

  private:
    uint64_t nextSeq() { return ++lruSeq; }

    void evictLRU();
    void remove(size_t idx);

    Fault translate(const RequestPtr &req, ThreadContext *tc,
                    BaseMMU::Translation *translation, BaseMMU::Mode mode,
                    bool &delayed);
    Fault doTranslate(const RequestPtr &req, ThreadContext *tc,
                      BaseMMU::Translation *translation, BaseMMU::Mode mode,
                      bool &delayed);
};

} // namespace RiscvISA
} // namespace gem5

#endif // __ARCH_RISCV_HTLB_HH__
