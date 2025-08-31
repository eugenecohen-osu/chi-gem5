/*
 * Copyright (c) 2012 ARM Limited
 * Copyright (c) 2020 Barkhausen Institut
 * Copyright (c) 2025 Oregon State University
 * All rights reserved.
 *
 * The license below extends only to copyright in the software and shall
 * not be construed as granting a license to any other intellectual
 * property including but not limited to intellectual property relating
 * to a hardware implementation of the functionality of the software
 * licensed hereunder.  You may use the software subject to the license
 * terms below provided that you ensure that this notice is replicated
 * unmodified and in its entirety in all distributions of the software,
 * modified or unmodified, in source code or in binary form.
 *
 * Copyright (c) 2007 The Hewlett-Packard Development Company
 * All rights reserved.
 *
 * The license below extends only to copyright in the software and shall
 * not be construed as granting a license to any other intellectual
 * property including but not limited to intellectual property relating
 * to a hardware implementation of the functionality of the software
 * licensed hereunder.  You may use the software subject to the license
 * terms below provided that you ensure that this notice is replicated
 * unmodified and in its entirety in all distributions of the software,
 * modified or unmodified, in source code or in binary form.
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

#include "arch/riscv/handletable_walker.hh"

#include <memory>

#include "arch/riscv/faults.hh"
//#include "arch/riscv/page_size.hh"
#include "arch/riscv/handletable.hh"
#include "arch/riscv/htlb.hh"
#include "base/bitfield.hh"
#include "base/trie.hh"
#include "cpu/base.hh"
#include "cpu/thread_context.hh"
#include "debug/HandleTableWalker.hh"
#include "mem/packet_access.hh"
#include "mem/request.hh"

namespace gem5
{

namespace RiscvISA {

Fault
HandleWalker::start(ThreadContext * _tc, BaseMMU::Translation *_translation,
    const RequestPtr &_req, BaseMMU::Mode _mode, HTlbEntry* result_entry)
{
    // TODO: in timing mode, instead of blocking when there are other
    // outstanding requests, see if this request can be coalesced with
    // another one (i.e. either coalesce or start walk)
    WalkerState * newState = new WalkerState(this, _translation, _req);
    newState->initState(_tc, _mode, sys->isTimingMode());
    if (currStates.size()) {
        assert(newState->isTiming());
        DPRINTF(HandleTableWalker, "Walks in progress: %d\n", currStates.size());
        currStates.push_back(newState);
        return NoFault;
    } else {
        currStates.push_back(newState);
        Fault fault = newState->walk();

        // Keep the resulting TLB entry
        // in some cases we might need to use the result
        // but not insert to the TLB, so we can't look it up if we return!
        if (result_entry)
            *result_entry = newState->entry;

        // In functional mode, always pop the state
        // In timing we must pop the state in the case of an early fault!
        if (fault != NoFault || !newState->isTiming())
        {
            currStates.pop_front();
            delete newState;
        }
        return fault;
    }
}

Fault
HandleWalker::startFunctional(ThreadContext * _tc, Addr &addr, unsigned &logBytes,
              BaseMMU::Mode _mode)
{
    funcState.initState(_tc, _mode);
    return funcState.startFunctional(addr, logBytes);
}

bool
HandleWalker::WalkerPort::recvTimingResp(PacketPtr pkt)
{
    return walker->recvTimingResp(pkt);
}

bool
HandleWalker::recvTimingResp(PacketPtr pkt)
{
    WalkerSenderState * senderState =
        dynamic_cast<WalkerSenderState *>(pkt->popSenderState());
    WalkerState * senderWalk = senderState->senderWalk;
    bool walkComplete = senderWalk->recvPacket(pkt);
    delete senderState;
    if (walkComplete) {
        std::list<WalkerState *>::iterator iter;
        for (iter = currStates.begin(); iter != currStates.end(); iter++) {
            WalkerState * walkerState = *(iter);
            if (walkerState == senderWalk) {
                iter = currStates.erase(iter);
                break;
            }
        }
        delete senderWalk;
        // Since we block requests when another is outstanding, we
        // need to check if there is a waiting request to be serviced
        if (currStates.size() && !startWalkWrapperEvent.scheduled())
            // delay sending any new requests until we are finished
            // with the responses
            schedule(startWalkWrapperEvent, clockEdge());
    }
    return true;
}

void
HandleWalker::WalkerPort::recvReqRetry()
{
    walker->recvReqRetry();
}

void
HandleWalker::recvReqRetry()
{
    std::list<WalkerState *>::iterator iter;
    for (iter = currStates.begin(); iter != currStates.end(); iter++) {
        WalkerState * walkerState = *(iter);
        if (walkerState->isRetrying()) {
            walkerState->retry();
        }
    }
}

bool HandleWalker::sendTiming(WalkerState* sendingState, PacketPtr pkt)
{
    WalkerSenderState* walker_state = new WalkerSenderState(sendingState);
    pkt->pushSenderState(walker_state);
    if (port.sendTimingReq(pkt)) {
        return true;
    } else {
        // undo the adding of the sender state and delete it, as we
        // will do it again the next time we attempt to send it
        pkt->popSenderState();
        delete walker_state;
        return false;
    }

}

Port &
HandleWalker::getPort(const std::string &if_name, PortID idx)
{
    if (if_name == "port")
        return port;
    else
        return ClockedObject::getPort(if_name, idx);
}

void
HandleWalker::WalkerState::initState(ThreadContext * _tc,
        BaseMMU::Mode _mode, bool _isTiming)
{
    assert(state == Ready);
    started = false;
    tc = _tc;
    mode = _mode;
    timing = _isTiming;
    // fetch these now in case they change during the walk
    memaccess = functional ?
        walker->htlb->getMemAccessInfo(tc, mode, (Request::ArchFlagsType)0):
        walker->htlb->getMemAccessInfo(tc, mode, req->getArchFlags());
    pmode = memaccess.priv;
    status = tc->readMiscReg(MISCREG_STATUS);
    MISA misa = tc->readMiscReg(MISCREG_ISA);

    // Find SATP
    // If no rvh or effective V = 0, base is SATP
    // otherwise base is VSATP (effective V=1)
    satp = (!misa.rvh || !memaccess.virt) ?
            tc->readMiscReg(MISCREG_SATP) :
            tc->readMiscReg(MISCREG_VSATP);

    // If effective V = 1, also read HGATP for
    // G-stage because we will perform a two-stage translation.
    hgatp = (misa.rvh && memaccess.virt) ?
            tc->readMiscReg(MISCREG_HGATP) :
            (RegVal)0;

    // TODO move this somewhere else
    // VSATP mode might be bare, but we still
    // will have to go through G-stage
    // assert(satp.mode == AddrXlateMode::SV39);

    // If functional entry.vhaddr will be set
    // in start functional (req == NULL)
    entry.vhaddr = functional ?
        (Addr)0 :
        req->getVaddr();

    entry.asid = satp.asid;
}

void
HandleWalker::startWalkWrapper()
{
    unsigned num_squashed = 0;
    WalkerState *currState = currStates.front();

    // check if we get a tlb hit to skip the walk
    Addr handle_id = getHandleIdFromVAddr( currState->req->getVaddr() );
    HTlbEntry *e = htlb->lookup(handle_id, currState->satp.asid, currState->mode,
                              true);
    Fault fault = NoFault;
    // should we do this for handles? TODO
    // if (e) {
    //    fault = htlb->checkPermissions(currState->tc, currState->memaccess,
    //                         e->vaddr, currState->mode, e->pte);
    // }

    while ((num_squashed < numSquashable) && currState &&
           (currState->translation->squashed() || (e && fault == NoFault))) {
        currStates.pop_front();
        num_squashed++;

        DPRINTF(HandleTableWalker, "Squashing table walk for address %#x\n",
            currState->req->getVaddr());

        // finish the translation which will delete the translation object
        if (currState->translation->squashed()) {
            currState->translation->finish(
                std::make_shared<UnimpFault>("Squashed Inst"),
                currState->req, currState->tc, currState->mode);
        } else {
            htlb->translateTiming(currState->req, currState->tc,
                                 currState->translation, currState->mode);
        }

        // delete the current request if there are no inflight packets.
        // if there is something in flight, delete when the packets are
        // received and inflight is zero.
        if (currState->numInflight() == 0) {
            delete currState;
        } else {
            currState->squash();
        }

        // check the next translation request, if it exists
        if (currStates.size()) {
            currState = currStates.front();
            Addr handle_id = getHandleIdFromVAddr( currState->req->getVaddr() );
            e = htlb->lookup(handle_id, currState->satp.asid, currState->mode,
                            true);
            // should we do this for handles? TODO
            // if (e) {
            //     
            //     fault = htlb->checkPermissions(currState->tc,
            //         currState->memaccess, e->vaddr, currState->mode, e->pte);
            // }
        } else {
            currState = NULL;
        }
    }
    if (currState && !currState->wasStarted()) {
        if (!e || fault != NoFault) {
            Fault timingFault = currState->walk();
            if (timingFault != NoFault) {
                currStates.pop_front();
                delete currState;
                currState = NULL;
            }
        }
        else {
            schedule(startWalkWrapperEvent, clockEdge(Cycles(1)));
        }
    }
}

Fault
HandleWalker::WalkerState::walk()
{
    Fault fault = NoFault;
    assert(!started);
    started = true;
    state = Translate;
    nextState = Ready;

    // This is the vaddr to walk for
    Addr vaddr = entry.vhaddr;

    // should we check if HTBASE is initialized HERE?? todo

    fault = walkOneStage(vaddr);

    return fault;
}


Fault
HandleWalker::WalkerState::walkOneStage(Addr vhaddr)
{
    level = 0;
    faulting_paddr = 0;

    if ( !isVaddrHandle(vhaddr)) {
        // no translation needed, we're done already
        DPRINTF(HandleTableWalker, "vaddr not a handle %#x", vhaddr);
        state = Ready;
        nextState = Waiting;
        return NoFault;
    }

    // calculate the handle index and check against bounds
    uint64_t ht0_index = getHt0Index(vhaddr);
    uint64_t ht_bound = tc->readMiscReg(MISCREG_HTBOUND);
    if (ht0_index > ht_bound) {
        DPRINTF(HandleTableWalker, "ht0 index %#x exceeds bounds %#x", ht0_index, ht_bound);
        return handleFault();
    }

    // create physical request for the HT0 entry
    Addr ht0_base = tc->readMiscReg(MISCREG_HTBASE);
    Addr ht0_entry_addr = ht0_base + (ht0_index * sizeof(HT_Entry));
    DPRINTF(HandleTableWalker, "reading ht0 addr %#x", ht0_entry_addr);
    read_packet = createReqPacket(ht0_entry_addr, MemCmd::ReadReq, sizeof(HT_Entry));

    if (timing)
    {
        nextState = state;
        state = Waiting;
        timingFault = NoFault;
        sendPackets();
        return NoFault;
    }

    Fault fault = NoFault;
    do
    {
        if (functional) {
            walker->port.sendFunctional(read_packet);
        }
        else {
            walker->port.sendAtomic(read_packet);
        }

        fault = stepWalk();
        assert(fault == NoFault || read_packet == NULL);
        state = nextState;
        nextState = Ready;
    } while (read_packet);

    state = Ready;
    nextState = Waiting;
    return fault;
}

Fault
HandleWalker::WalkerState::startFunctional(Addr &addr, unsigned &logBytes)
{
    entry.vhaddr = addr;
    return walk();
}

Fault
HandleWalker::WalkerState::stepWalk(void)
{
    assert(state != Ready && state != Waiting);

    Fault fault = NoFault;
    HT_Entry hte = read_packet->getLE<HT_Entry>();
    Addr nextRead = 0;

    // walk flags are initialized to false
    WalkFlags stepWalkFlags;

    DPRINTF(HandleTableWalker, "Got level%d HTE: %#x\n", level, hte);

    // step 2:
    // Performing PMA/PMP checks on physical address of HTE

    // fault = walker->pmp->pmpCheck(read->req, BaseMMU::Read,
    //                 RiscvISA::PrivilegeMode::PRV_S, tc, entry.vaddr);
    // if (fault != NoFault) goto early_exit;
    
    // fault = walker->pma->check(read->req, BaseMMU::Read, entry.vaddr);
    // if (fault != NoFault) goto early_exit;

    if (level == 0) {
        level++;
    
        walker->handlewalkerstats.num_ht0_walks++;

        // is hte null?
        if (hte == 0) {
            DPRINTF(HandleTableWalker, "HT0 entry at %#x is null", read_packet->req->getPaddr());
            return handleFault();
        }

        // calculate the address given the HT0 entry and HT1 index
        uint64_t ht1_index = getHt1Index(entry.vhaddr);
        assert (ht1_index < HT_TABLE_MAX_ENTRIES);
        Addr ht1_entry_addres = hte + (ht1_index * sizeof(HT_Entry));

        DPRINTF(HandleTableWalker, "Reading HT1 at %#x", ht1_entry_addres);
        nextRead = ht1_entry_addres;
        nextState = Translate;

    } else if (level == 1) {

        walker->handlewalkerstats.num_ht1_walks++;

        DPRINTF(HandleTableWalker, "HT1 entry %#x has vaddr %#x", nextRead, hte);

        // finalize tlb entry with by aligning to handle base
        entry.vhaddr = getHandleBaseVHAddr(entry.vhaddr);
        entry.vaddr = hte;

        // Also don't insert on special_access
        if (!memaccess.bypassTLB())
            stepWalkFlags.doTLBInsert = true;

    } else { // bad level
            stepWalkFlags.doEndWalk = true;
            fault = handleFault();
    }
        

    PacketPtr oldRead = read_packet;
    Request::Flags flags = oldRead->req->getFlags();

    // If we didn't jump to early_exit, we're setting up another read.
    RequestPtr request = std::make_shared<Request>(
        nextRead, oldRead->getSize(), flags, walker->requestorId);

    delete oldRead;
    oldRead = nullptr;

    read_packet = new Packet(request, MemCmd::ReadReq);
    read_packet->allocate();

    if (stepWalkFlags.doEndWalk) {

        if (stepWalkFlags.doTLBInsert) {
            if (!functional && !memaccess.bypassTLB()) {
                Addr handle_id = getHandleIdFromVAddr( entry.vhaddr );
                walker->htlb->insert(handle_id, entry);
            }
        }
        endWalk();
    }

    return fault;
}

void
HandleWalker::WalkerState::endWalk()
{
    nextState = Ready;
    delete read_packet;
    read_packet = NULL;
}

bool
HandleWalker::WalkerState::recvPacket(PacketPtr pkt)
{
    assert(pkt->isResponse());
    assert(inflight);
    assert(state == Waiting);
    inflight--;
    if (squashed) {
        // if were were squashed, return true once inflight is zero and
        // this WalkerState will be freed there.
        return (inflight == 0);
    }
    if (pkt->isRead()) {
        // should not have a pending read it we also had one outstanding
        assert(!read_packet);

        // @todo someone should pay for this
        pkt->headerDelay = pkt->payloadDelay = 0;

        state = nextState;
        nextState = Ready;
        read_packet = pkt;
        timingFault = stepWalk();
        state = Waiting;
        assert(timingFault == NoFault || read_packet == NULL);
        sendPackets();
    } else {
        delete pkt;

        sendPackets();
    }
    if (inflight == 0 && read_packet == NULL) {
        state = Ready;
        nextState = Waiting;
        if (timingFault == NoFault) {
            /*
             * Finish the translation. Now that we know the right entry is
             * in the TLB, this should work with no memory accesses.
             * There could be new faults unrelated to the table walk like
             * permissions violations, so we'll need the return value as
             * well.
             */
            Addr vaddr = req->getVaddr();
            vaddr = Addr(sext<SV39_VADDR_BITS>(vaddr));
            Addr paddr = walker->htlb->hiddenTranslateWithTLB(vaddr, satp.asid, mode);

            req->setPaddr(paddr);

            // do pmp check if any checking condition is met.
            // timingFault will be NoFault if pmp checks are
            // passed, otherwise an address fault will be returned.
            timingFault = walker->pmp->pmpCheck(req, mode, pmode, tc);

            if (timingFault == NoFault) {
                timingFault = walker->pma->check(req, mode);
            }

            // Let the CPU continue.
            translation->finish(timingFault, req, tc, mode);
        } else {
            // There was a fault during the walk. Let the CPU know.
            translation->finish(timingFault, req, tc, mode);
        }
        return true;
    }

    return false;
}

void
HandleWalker::WalkerState::sendPackets()
{
    //If we're already waiting for the port to become available, just return.
    if (retrying)
        return;

    //Reads always have priority
    if (read_packet) {
        PacketPtr pkt = read_packet;
        read_packet = NULL;
        inflight++;
        if (!walker->sendTiming(this, pkt)) {
            retrying = true;
            read_packet = pkt;
            inflight--;
            return;
        }
    }
}

PacketPtr
HandleWalker::WalkerState::createReqPacket(Addr paddr, MemCmd cmd, size_t bytes)
{
    Request::Flags flags = Request::PHYSICAL;
    RequestPtr request = std::make_shared<Request>(
        paddr, bytes, flags, walker->requestorId);
    PacketPtr pkt = new Packet(request, cmd);
    pkt->allocate();
    return pkt;
}

unsigned
HandleWalker::WalkerState::numInflight() const
{
    return inflight;
}

bool
HandleWalker::WalkerState::isRetrying()
{
    return retrying;
}

bool
HandleWalker::WalkerState::isTiming()
{
    return timing;
}

bool
HandleWalker::WalkerState::wasStarted()
{
    return started;
}

void
HandleWalker::WalkerState::squash()
{
    squashed = true;
}

void
HandleWalker::WalkerState::retry()
{
    retrying = false;
    sendPackets();
}

Fault
HandleWalker::WalkerState::handleFault()
{
    return walker->htlb->createHandlefault(entry.vhaddr, mode, faulting_paddr);
}

HandleWalker::HandlewalkerStats::HandlewalkerStats(statistics::Group *parent)
  : statistics::Group(parent),
    ADD_STAT(num_ht0_walks, statistics::units::Count::get(),
             "Completed level 0 handle table walks"),
    ADD_STAT(num_ht1_walks, statistics::units::Count::get(),
             "Completed level 1 handle table walks")
{
}

} // namespace RiscvISA
} // namespace gem5
