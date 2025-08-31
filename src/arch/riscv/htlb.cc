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

#include "arch/riscv/tlb.hh"

#include <string>
#include <vector>

#include "arch/riscv/faults.hh"
#include "arch/riscv/memflags.hh"
#include "arch/riscv/hmmu.hh"
#include "arch/riscv/handletable.hh"
#include "arch/riscv/handletable_walker.hh"
#include "arch/riscv/pma_checker.hh"
#include "arch/riscv/pmp.hh"
#include "arch/riscv/pra_constants.hh"
#include "arch/riscv/process.hh"
#include "arch/riscv/utility.hh"
#include "base/inifile.hh"
#include "base/str.hh"
#include "base/trace.hh"
#include "cpu/thread_context.hh"
#include "debug/HTLB.hh"
#include "debug/HTLBVerbose.hh"
#include "params/RiscvHTLB.hh"
#include "sim/full_system.hh"
#include "sim/process.hh"
#include "sim/system.hh"

namespace gem5
{

using namespace RiscvISA;

static Addr
buildKey(Addr handle_id, uint16_t asid)
{
    // Note ASID is 16 bits
    // The handle_id is 31 bits
    // so our key is: 63:48 asid : 47:16 zeros : 30:0 handle_id
    return (static_cast<Addr>(asid) << 48) | handle_id;
}

HTLB::HTLB(const Params &p) :
    BaseTLB(p), size(p.size), tlb(size),
    lruSeq(0), stats(this), pma(p.pma_checker),
    pmp(p.pmp)
{
    for (size_t x = 0; x < size; x++) {
        tlb[x].trieHandle = NULL;
        freeList.push_back(&tlb[x]);
    }

    walker = p.walker;
    walker->setTLB(this);
}

HandleWalker *
HTLB::getWalker()
{
    return walker;
}

void
HTLB::evictLRU()
{
    // Find the entry with the lowest (and hence least recently updated)
    // sequence number.

    size_t lru = 0;
    for (size_t i = 1; i < size; i++) {
        if (tlb[i].lruSeq < tlb[lru].lruSeq)
            lru = i;
    }

    remove(lru);
}

HTlbEntry *
HTLB::lookup(Addr handle_id, uint16_t asid, BaseMMU::Mode mode, bool hidden)
{
    HTlbEntry *entry = trie.lookup(buildKey(handle_id, asid));

    DPRINTF(HTLBVerbose, "lookup(handle_id=%#x, asid=%#x): "
                        "%s vaddr=%#x %s\n",
            handle_id, asid, entry ? "hit" : "miss",
            entry ? entry->vaddr : 0, hidden ? "hidden" : "");

    if (!hidden) {
        if (entry)
            entry->lruSeq = nextSeq();

        if (mode == BaseMMU::Write)
            stats.writeAccesses++;
        else
            stats.readAccesses++;

        if (!entry) {
            if (mode == BaseMMU::Write)
                stats.writeMisses++;
            else
                stats.readMisses++;
        }
        else {
            if (mode == BaseMMU::Write)
                stats.writeHits++;
            else
                stats.readHits++;
        }
    }

    return entry;
}

HTlbEntry *
HTLB::insert(Addr handle_id, const HTlbEntry &entry)
{
    DPRINTF(HTLB, "insert(handle_id=%#x, asid=%#x, key=%#x): "
                 "vhaddr=%#x vaddr=%#x\n",
        handle_id, entry.asid, buildKey(handle_id, entry.asid), entry.vhaddr, entry.vaddr);

    // If somebody beat us to it, just use that existing entry.
    HTlbEntry *newEntry = lookup(handle_id, entry.asid, BaseMMU::Read, true);
    if (newEntry) {
        assert(newEntry->vhaddr == entry.vhaddr);
        assert(newEntry->vaddr == entry.vaddr);
        assert(newEntry->asid == entry.asid);
        return newEntry;
    }

    if (freeList.empty())
        evictLRU();

    newEntry = freeList.front();
    freeList.pop_front();

    Addr key = buildKey(handle_id, entry.asid);
    *newEntry = entry;
    newEntry->lruSeq = nextSeq();
    // CHECK THIS - IS MAXBITS preserving the key???
    newEntry->trieHandle = trie.insert(key, TlbEntryTrie::MaxBits, newEntry);
    return newEntry;
}

void
HTLB::demapPage(Addr vaddr, uint64_t asid)
{
    asid &= 0xFFFF;

    DPRINTF(HTLB, "flush(vaddr=%#x, asid=%#x)\n", vaddr, asid);
    if (vaddr == 0 && asid == 0) {
        DPRINTF(HTLB, "Flushing all HTLB entries\n");
        flushAll();
    } else {
        if (vaddr != 0 && asid != 0) {
            Addr handle_id = getHandleIdFromVAddr(vaddr);
            HTlbEntry *entry = lookup(handle_id, asid, BaseMMU::Read, true);
            if (entry) {
                remove(entry - tlb.data());
            }
        }
        else {
            for (size_t i = 0; i < size; i++) {
                if (tlb[i].trieHandle) {
                    if ((vaddr == 0 || (vaddr & HANDLE_ID_MASK) == tlb[i].vaddr) &&
                        (asid == 0 || tlb[i].asid == asid))
                        remove(i);
                }
            }
        }
    }
}

void
HTLB::flushAll()
{
    DPRINTF(HTLB, "flushAll()\n");
    for (size_t i = 0; i < size; i++) {
        if (tlb[i].trieHandle)
            remove(i);
    }
}

void
HTLB::remove(size_t idx)
{
    DPRINTF(HTLB, "remove(vhaddr=%#x, asid=%#x): vaddr=%#x\n",
        tlb[idx].vhaddr, tlb[idx].asid, tlb[idx].vaddr);

    assert(tlb[idx].trieHandle);
    trie.remove(tlb[idx].trieHandle);
    tlb[idx].trieHandle = NULL;
    freeList.push_back(&tlb[idx]);
}

// Fault
// HTLB::checkPermissions(ThreadContext* tc, MemAccessInfo mem_access, Addr vaddr,
//             BaseMMU::Mode mode, PTESv39 pte, Addr gvaddr, XlateStage stage)
// {
//     MISA misa = tc->readMiscReg(MISCREG_ISA);
//     STATUS status = tc->readMiscReg(MISCREG_STATUS);
//     PrivilegeMode priv = stage == XlateStage::GSTAGE ?
//         PRV_U : mem_access.priv;

//     bool sum = status.sum;
//     bool mxr = status.mxr;
//     bool gpf = stage == GSTAGE;
//     bool virt = mem_access.virt;

//     bool pf = false;

//     if (misa.rvh && mem_access.virt && stage == FIRST_STAGE) {
//         STATUS vsstatus = tc->readMiscReg(MISCREG_VSSTATUS);
//         sum = vsstatus.sum;
//         mxr |= vsstatus.mxr;
//     }


//     if (mem_access.hlvx) {
//         if (!pte.x) {
//             pf = true; DPRINTF(HTLB, "HLVX with no exec perm, raising PF\n");
//         }
//     }
//     else if (mode == BaseMMU::Read && !pte.r) {
//         if (mxr && pte.x) {
//             DPRINTF(HTLBVerbose, "MXR bit on, load from exec page success\n");
//         }
//         else {
//             pf = true; DPRINTF(HTLB, "PTE has no read perm, raising PF\n");
//         }
//     }
//     else if (mode == BaseMMU::Write && !pte.w) {
//         pf = true; DPRINTF(HTLB, "PTE has no write perm, raising PF\n");
//     }
//     else if (mode == BaseMMU::Execute && !pte.x) {
//         pf = true; DPRINTF(HTLB, "PTE has no exec perm, raising PF\n");
//     }

//     if (!pf) {
//         // check pte.u
//         if (priv == PRV_U && !pte.u) {
//             pf = true; DPRINTF(HTLB, "PTE not user accessible, raising PF\n");
//         }
//         else if (priv == PRV_S && pte.u &&
//                 (mode == BaseMMU::Execute || sum == 0)) {
//             pf = true; DPRINTF(HTLB, "PTE only user accessible, raising PF\n");
//         }
//     }

//     return pf ? createPagefault(vaddr, mode, gvaddr, gpf, virt) : NoFault;
// }

Fault
HTLB::createHandlefault(Addr vhaddr, BaseMMU::Mode mode,
                     Addr gpaddr, bool virt)
{
    ExceptionCode code;
    if (mode == BaseMMU::Read) {
        code = ExceptionCode::LOAD_PAGE;
    }
    else if (mode == BaseMMU::Write) {
        code = ExceptionCode::STORE_PAGE;
    }
    else {
        code = ExceptionCode::INST_PAGE;
    }

    return std::make_shared<AddressFault>(vhaddr, code, gpaddr, virt);
}

Addr
HTLB::hiddenTranslateWithTLB(Addr vhaddr, uint16_t asid, BaseMMU::Mode mode)
{
    Addr handle_id = getHandleIdFromVAddr(vhaddr);
    HTlbEntry *e = lookup(handle_id, asid, mode, true);
    assert(e != nullptr);
    // CHECK THIS TODO !!!
    return e->vaddr + getOffsetFromVHAddr(vhaddr);
}

Fault
HTLB::doTranslate(const RequestPtr &req, ThreadContext *tc,
                 BaseMMU::Translation *translation, BaseMMU::Mode mode,
                 bool &delayed)
{
    delayed = false;

    MemAccessInfo memaccess = getMemAccessInfo(tc, mode, req->getArchFlags());
    Addr vhaddr = req->getVaddr();

    // handle flag not set, nothing to do
    if (!isVaddrHandle(vhaddr)) {
        return NoFault;
    }

    // get handle id from vhaddr
    Addr handle_id = getHandleIdFromVAddr(vhaddr);

    MISA misa = tc->readMiscReg(MISCREG_ISA);

    SATP satp = (misa.rvh && memaccess.virt) ?
        tc->readMiscReg(MISCREG_VSATP) :
        tc->readMiscReg(MISCREG_SATP);

    HTlbEntry *e = nullptr;
    if (!memaccess.bypassTLB()) {
        e = lookup(handle_id, satp.asid, mode, false);
        if (!e) {
            Fault fault = walker->start(tc, translation, req, mode);
            // Atomic translations have translation == nullptr
            // so the if body is reachable only in timing
            if (translation != nullptr) {
                // If there has been a fault already, do not
                // mark the translation as delayed as that
                // will block its deletion
                if (fault != NoFault) {
                    delayed = false;
                } else {
                    delayed = true;
                }
                return fault;
            }
            else if (fault != NoFault) {
                return fault;
            }
            e = lookup(handle_id, satp.asid, mode, true);
            assert(e != nullptr);
        }
    }
    else {
        // Don't lookup and don't insert when bypassing the TLB.
        // We get the translation result back in memory pointed to by
        // HTlbEntry *e which is not inserted!
        e = new HTlbEntry();
        Fault fault = walker->start(tc, translation, req, mode, e);

        if (translation != nullptr || fault != NoFault) {
            // This gets ignored in atomic mode.
            delayed = true;
            return fault;
        }
    }

    Fault fault = NoFault;

    // handle permissions not yet implemented!
    // if (memaccess.bypassTLB()) {
    //     fault = NoFault;
    // }
    // else {
    //     handle permissions not yet implemented!
    //     if (memaccess.virt) {
    //         if (e->gpte != 0) {
    //             fault = checkPermissions(
    //                 tc, memaccess, vhaddr, mode, e->gpte);
    //         } else {
    //             fault = NoFault;
    //         }
    //     }
    //     else {
    //         fault = checkPermissions(
    //             tc, memaccess, vaddr, mode, e->pte);
    //     }
    // }


    // CHECK THIS TODO!
    Addr vaddr = e->vaddr + getOffsetFromVHAddr(vhaddr);

    DPRINTF(HTLBVerbose, "translate(vhaddr=%#x, handle_id=%#x, asid=%#x): %#x\n",
            vhaddr, handle_id, satp.asid, vaddr);
    
    // we replace the handle-vaddr with the normal vaddr
    req->setVaddr(vaddr);

    if (memaccess.bypassTLB())
        delete e;

    return NoFault;
}

MemAccessInfo
HTLB::getMemAccessInfo(ThreadContext *tc, BaseMMU::Mode mode,
        const Request::ArchFlagsType arch_flags)
{
    // this is probably not right for handle memory access, TODO!!

    MISA misa = tc->readMiscReg(MISCREG_ISA);
    STATUS status = tc->readMiscReg(MISCREG_STATUS);
    HSTATUS hstatus = tc->readMiscReg(MISCREG_HSTATUS);
    PrivilegeMode priv = (PrivilegeMode)tc->readMiscReg(MISCREG_PRV);

    bool virt = misa.rvh ? virtualizationEnabled(tc) : false;
    bool force_virt = false;
    bool hlvx = false;
    bool lr = false;

    if (mode != BaseMMU::Execute && status.mprv == 1) {
        priv = (PrivilegeMode)(RegVal)status.mpp;
        if (misa.rvh && status.mpv && priv != PRV_M) {
            virt = true;
        }
    }

    if (misa.rva) {
        if (arch_flags & XlateFlags::LR) {
            lr = true;
        }
    }

    if (misa.rvh) {
        if (arch_flags & XlateFlags::FORCE_VIRT) {
            priv = (PrivilegeMode)(RegVal)hstatus.spvp;
            virt = true;
            force_virt = true;
        }
        if (arch_flags & XlateFlags::HLVX) {
            hlvx = true;
        }
    }
    return MemAccessInfo(priv, virt, force_virt, hlvx, lr);
}

Fault
HTLB::translate(const RequestPtr &req, ThreadContext *tc,
               BaseMMU::Translation *translation, BaseMMU::Mode mode,
               bool &delayed)
{
    delayed = false;

    if (FullSystem) {
        MemAccessInfo memaccess = getMemAccessInfo(
            tc, mode, req->getArchFlags());
        PrivilegeMode pmode = memaccess.priv;
        MISA misa = tc->readMiscRegNoEffect(MISCREG_ISA);
        SATP satp = (misa.rvh && memaccess.virt) ?
            tc->readMiscReg(MISCREG_VSATP) :
            tc->readMiscReg(MISCREG_SATP);

        Fault fault = NoFault;

        // TODO: is this needed for handles???
        fault = pma->checkVAddrAlignment(req, mode);

        // we should check if we're in physical mode and bypass handle translation

        // this is already in tlb.cc
        if (!misa.rvs || pmode == PrivilegeMode::PRV_M ||
            satp.mode == AddrXlateMode::BARE) {

            // In H-Extension there is the case for VS mode
            // that SATP's mode is BARE but we still have
            // to check if we need to perform G-stage (2nd stage)
            // translation. The request is PHYSICAL only if
            // HGATP's mode is also BARE, else we perform
            // the G-stage translation.
            if (misa.rvh && memaccess.virt) {
                SATP hgatp = tc->readMiscReg(MISCREG_HGATP);
                if (hgatp.mode == AddrXlateMode::BARE) {
                    req->setFlags(Request::PHYSICAL);
                }
            }
            else {
                req->setFlags(Request::PHYSICAL);
            }
        }

        if (fault == NoFault) {
            if (req->getFlags() & Request::PHYSICAL) {
                // we just leave the request alone and let the MMU do the rest
            } else {
                fault = doTranslate(req, tc, translation, mode, delayed);
            }
        }

        // should we remove this for handle since MMU will do the check?? TODO
        if (!delayed && fault == NoFault) {
            // do pmp check if any checking condition is met.
            // timingFault will be NoFault if pmp checks are
            // passed, otherwise an address fault will be returned.
            fault = pmp->pmpCheck(req, mode, pmode, tc);
        }

        // should we remove this for handle since MMU will do the check?? TODO
        if (!delayed && fault == NoFault) {
            fault = pma->check(req, mode);
        }
        return fault;
    } else { // not FullSystem

        Fault fault = NoFault;
        Addr vhaddr = getValidAddr(req->getVaddr(), tc, mode);

        // if the vaddr is a handle address
        if (isVaddrHandle(vhaddr)) {
            // translate the handle vaddr to a normal vaddr, replacing the vaddr in the req
            DPRINTF(HTLB, "vaddr is HANDLE, translating %#x", vhaddr);
            fault = doTranslate(req, tc, translation, mode, delayed);
        }

        return fault;
    }
}

Fault
HTLB::translateAtomic(const RequestPtr &req, ThreadContext *tc,
                     BaseMMU::Mode mode)
{
    bool delayed;
    return translate(req, tc, nullptr, mode, delayed);
}

void
HTLB::translateTiming(const RequestPtr &req, ThreadContext *tc,
                     BaseMMU::Translation *translation, BaseMMU::Mode mode)
{
    bool delayed;
    assert(translation);
    Fault fault = translate(req, tc, translation, mode, delayed);
    if (!delayed)
        translation->finish(fault, req, tc, mode);
    else
        translation->markDelayed();
}

Fault
HTLB::translateFunctional(const RequestPtr &req, ThreadContext *tc,
                         BaseMMU::Mode mode)
{
    const Addr vhaddr = getValidAddr(req->getVaddr(), tc, mode);
    Addr vaddr = vhaddr; // vhaddr in, vaddr out

    HMMU *mmu = static_cast<HMMU *>(tc->getMMUPtr());

    // if full system check MISA/SATP
    if (FullSystem) {
        MemAccessInfo memaccess = getMemAccessInfo(
            tc, mode, req->getArchFlags());
        PrivilegeMode pmode = memaccess.priv;
        MISA misa = tc->readMiscRegNoEffect(MISCREG_ISA);
        SATP satp = tc->readMiscReg(MISCREG_SATP);
        if (!misa.rvs || pmode == PrivilegeMode::PRV_M ||
            satp.mode == AddrXlateMode::BARE) {
                // no handle translation
                return NoFault;
        }
    }

    HandleWalker *walker = mmu->getHandleWalker();
    unsigned logBytes;
    
    Fault fault = walker->startFunctional(
            tc, vaddr, logBytes, mode);
    if (fault != NoFault)
        return fault;

    // CHECK THIS TODO
    vaddr += getOffsetFromVHAddr(vhaddr);
    
    DPRINTF(HTLB, "Translated (functional) %#x -> %#x.\n", vhaddr, vaddr);
    req->setVaddr(vaddr);
    return NoFault;
}

Fault
HTLB::finalizePhysical(const RequestPtr &req,
                      ThreadContext *tc, BaseMMU::Mode mode) const
{
    return NoFault;
}

void
HTLB::serialize(CheckpointOut &cp) const
{
    // Only store the entries in use.
    uint32_t _size = size - freeList.size();
    SERIALIZE_SCALAR(_size);
    SERIALIZE_SCALAR(lruSeq);

    uint32_t _count = 0;
    for (uint32_t x = 0; x < size; x++) {
        if (tlb[x].trieHandle != NULL)
            tlb[x].serializeSection(cp, csprintf("Entry%d", _count++));
    }
}

void
HTLB::unserialize(CheckpointIn &cp)
{
    // Do not allow to restore with a smaller tlb.
    uint32_t _size;
    UNSERIALIZE_SCALAR(_size);
    if (_size > size) {
        fatal("TLB size less than the one in checkpoint!");
    }

    UNSERIALIZE_SCALAR(lruSeq);

    for (uint32_t x = 0; x < _size; x++) {
        HTlbEntry *newEntry = freeList.front();
        freeList.pop_front();

        newEntry->unserializeSection(cp, csprintf("Entry%d", x));
        // TODO: When supporting other addressing modes fix this
        Addr handle_id = getHandleIdFromVAddr(newEntry->vaddr);
        Addr key = buildKey(handle_id, newEntry->asid);
        newEntry->trieHandle = trie.insert(key,
            HTlbEntryTrie::MaxBits, newEntry);
    }
}

HTLB::TlbStats::TlbStats(statistics::Group *parent)
  : statistics::Group(parent),
    ADD_STAT(readHits, statistics::units::Count::get(), "read hits"),
    ADD_STAT(readMisses, statistics::units::Count::get(), "read misses"),
    ADD_STAT(readAccesses, statistics::units::Count::get(), "read accesses"),
    ADD_STAT(writeHits, statistics::units::Count::get(), "write hits"),
    ADD_STAT(writeMisses, statistics::units::Count::get(), "write misses"),
    ADD_STAT(writeAccesses, statistics::units::Count::get(), "write accesses"),
    ADD_STAT(hits, statistics::units::Count::get(),
             "Total TLB (read and write) hits", readHits + writeHits),
    ADD_STAT(misses, statistics::units::Count::get(),
             "Total TLB (read and write) misses", readMisses + writeMisses),
    ADD_STAT(accesses, statistics::units::Count::get(),
             "Total TLB (read and write) accesses",
             readAccesses + writeAccesses)
{
}

Port *
HTLB::getTableWalkerPort()
{
    return &walker->getPort("port");
}

} // namespace gem5
