/*
 * Copyright (c) 2002-2005 The Regents of The University of Michigan
 * Copyright (c) 2007 MIPS Technologies, Inc.
 * Copyright (c) 2020 Barkhausen Institut
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

#ifndef __ARCH_RISCV_HANDLETABLE_H__
#define __ARCH_RISCV_HANDLETABLE_H__

#include "base/bitunion.hh"
#include "base/logging.hh"
#include "base/trie.hh"
#include "base/types.hh"
#include "sim/serialize.hh"

namespace gem5
{

namespace RiscvISA {

// bit in virtual address indicating a handle
static constexpr Addr HANDLE_VADDR_FLAG = 0x8000000000000000ull;

// portion of virtual address for handle id
static constexpr Addr HANDLE_ID_MASK = 0x7FFFFFFF00000000ull;
static constexpr Addr HANDLE_ID_SHIFT = 32;

// portion of virtual address for handle offset
static constexpr Addr HANDLE_OFFSET_MASK = mask(32);

// HT0 entry is a 64-bit physical address of the HT1 table
// HT1 entry is a 64-bit virtual address of the memory
using HT_Entry = uint64_t;

static inline bool isVaddrHandle(Addr vaddr)
{
    return (vaddr & HANDLE_VADDR_FLAG) == HANDLE_VADDR_FLAG;
}

static inline Addr getHandleIdFromVAddr(Addr vaddr)
{
    return bits(vaddr, 62, 32);
}

static constexpr Addr HT_TABLE_SIZE_BITS = 21; // log2( 2MB )
static constexpr Addr HT_TABLE_SIZE = (1 << HT_TABLE_SIZE_BITS);
static constexpr Addr HT_TABLE_MAX_ENTRIES = HT_TABLE_SIZE / sizeof(HT_Entry);

static inline uint64_t getHt0Index(Addr vhaddr)
{
    vhaddr &= HANDLE_ID_MASK;
    // HT0 index is the upper 13 bits of the handle id
    return bits(vhaddr, 63, 50);
}

static inline uint64_t getHt1Index(Addr vhaddr)
{
    vhaddr &= HANDLE_ID_MASK;
    // HT1 index is the lower 18 bits of the handle id
    return bits(vhaddr, 49, 32);
}

static inline Addr getHandleBaseVHAddr(Addr vhaddr)
{
    return vhaddr & HANDLE_ID_MASK;
}

static inline Addr getOffsetFromVHAddr(Addr vhaddr)
{
    return vhaddr & HANDLE_OFFSET_MASK;
}

struct HTlbEntry;
typedef Trie<Addr, HTlbEntry> HTlbEntryTrie;

struct HTlbEntry : public Serializable
{
    // The handle virtual address (handle flag set).
    Addr vhaddr;

    // The normal virtual address (handle flag clear).
    Addr vaddr;

    uint16_t asid;

    HTlbEntryTrie::Handle trieHandle;

    // A sequence number to keep track of LRU.
    uint64_t lruSeq;

    HTlbEntry()
        : vhaddr(0), vaddr(0), lruSeq(0)
    {}

    void reset()
    {
        vhaddr = vaddr = lruSeq = 0;
    }

    void serialize(CheckpointOut &cp) const override;
    void unserialize(CheckpointIn &cp) override;
};

} // namespace RiscvISA
} // namespace gem5

#endif // __ARCH_RISCV_HANDLETABLE_H__
