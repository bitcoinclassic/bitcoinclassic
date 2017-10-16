/*
 * This file is part of the bitcoin-classic project
 * Copyright (c) 2009-2010 Satoshi Nakamoto
 * Copyright (c) 2009-2015 The Bitcoin Core developers
 * Copyright (C) 2017 Tom Zander <tomz@freedommail.ch>
 * Copyright (C) 2017 Angel Leon <@gubatron>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "InvMessage.h"

namespace Network {
    bool InvMessage::handle(CNode *const pfrom,
                            CDataStream &vRecv,
                            int64_t nTimeReceived,
                            std::string &strCommand,
                            const bool xthinEnabled,
                            const bool fReindexing) {
        std::vector<CInv> vInv;
        vRecv >> vInv;
        if (vInv.size() > MAX_INV_SZ) {
            Misbehaving(pfrom->GetId(), 20);
            return error("message inv size() = %u", vInv.size());
        }

        bool fBlocksOnly = GetBoolArg("-blocksonly", DEFAULT_BLOCKSONLY);

        // Allow whitelisted peers to send data other than blocks in blocks only mode if whitelistrelay is true
        if (pfrom->fWhitelisted && GetBoolArg("-whitelistrelay", DEFAULT_WHITELISTRELAY))
            fBlocksOnly = false;

        LOCK(cs_main);

        std::vector<CInv> vToFetch;

        for (unsigned int nInv = 0; nInv < vInv.size(); nInv++) {
            const CInv &inv = vInv[nInv];

            boost::this_thread::interruption_point();
            pfrom->AddInventoryKnown(inv);

            bool fAlreadyHave = AlreadyHave(inv);
            logDebug(Log::Net) << "got inv:" << inv << (fAlreadyHave ? "have" : "new") << "peer:" << pfrom->id;

            if (inv.type == MSG_BLOCK) {
                UpdateBlockAvailability(pfrom->GetId(), inv.hash);
                if (!fAlreadyHave && !fImporting && !fReindexing && !mapBlocksInFlight.count(inv.hash)) {
                    // First request the headers preceding the announced block. In the normal fully-synced
                    // case where a new block is announced that succeeds the current tip (no reorganization),
                    // there are no such headers.
                    // Secondly, and only when we are close to being synced, we request the announced block directly,
                    // to avoid an extra round-trip. Note that we must *first* ask for the headers, so by the
                    // time the block arrives, the header chain leading up to it is already validated. Not
                    // doing this will result in the received block being rejected as an orphan in case it is
                    // not a direct successor.
                    pfrom->PushMessage(NetMsgType::GETHEADERS, chainActive.GetLocator(pindexBestHeader), inv.hash);
                    const CChainParams chainparams = Params();
                    CNodeState *nodestate = State(pfrom->GetId());
                    if (CanDirectFetch(chainparams.GetConsensus()) &&
                        nodestate->nBlocksInFlight < MAX_BLOCKS_IN_TRANSIT_PER_PEER) {
                        // BUIP010 Xtreme Thinblocks: begin section
                        CInv inv2(inv);
                        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
                        if (xthinEnabled && IsChainNearlySyncd()) {
                            if (HaveThinblockNodes() && CheckThinblockTimer(inv.hash)) {
                                // Must download a block from a ThinBlock peer
                                if (pfrom->mapThinBlocksInFlight.size() < 1 &&
                                    pfrom->ThinBlockCapable()) { // We can only send one thinblock per peer at a time
                                    pfrom->mapThinBlocksInFlight[inv2.hash] = GetTime();
                                    inv2.type = MSG_XTHINBLOCK;
                                    CBloomFilter filterMemPool = createSeededBloomFilter(
                                            CTxOrphanCache::instance()->fetchTransactionIds());
                                    ss << inv2;
                                    ss << filterMemPool;
                                    pfrom->PushMessage(NetMsgType::GET_XTHIN, ss);
                                    MarkBlockAsInFlight(pfrom->GetId(), inv.hash, chainparams.GetConsensus());
                                    LogPrint("thin", "Requesting Thinblock %s from peer %s (%d)\n",
                                             inv2.hash.ToString(), pfrom->addrName.c_str(), pfrom->id);
                                }
                            } else {
                                // Try to download a thinblock if possible otherwise just download a regular block
                                if (pfrom->mapThinBlocksInFlight.size() < 1 &&
                                    pfrom->ThinBlockCapable()) { // We can only send one thinblock per peer at a time
                                    pfrom->mapThinBlocksInFlight[inv2.hash] = GetTime();
                                    inv2.type = MSG_XTHINBLOCK;
                                    CBloomFilter filterMemPool = createSeededBloomFilter(
                                            CTxOrphanCache::instance()->fetchTransactionIds());
                                    ss << inv2;
                                    ss << filterMemPool;
                                    pfrom->PushMessage(NetMsgType::GET_XTHIN, ss);
                                    LogPrint("thin", "Requesting Thinblock %s from peer %s (%d)\n",
                                             inv2.hash.ToString(), pfrom->addrName.c_str(), pfrom->id);
                                } else {
                                    LogPrint("thin", "Requesting Regular Block %s from peer %s (%d)\n",
                                             inv2.hash.ToString(), pfrom->addrName.c_str(), pfrom->id);
                                    vToFetch.push_back(inv2);
                                }
                                MarkBlockAsInFlight(pfrom->GetId(), inv.hash, chainparams.GetConsensus());
                            }
                        } else {
                            vToFetch.push_back(inv2);
                            MarkBlockAsInFlight(pfrom->GetId(), inv.hash, chainparams.GetConsensus());
                            LogPrint("thin", "Requesting Regular Block %s from peer %s (%d)\n", inv2.hash.ToString(),
                                     pfrom->addrName.c_str(), pfrom->id);
                        }
                        // BUIP010 Xtreme Thinblocks: end section
                    }
                    LogPrint("net", "getheaders (%d) %s to peer=%d\n", pindexBestHeader->nHeight, inv.hash.ToString(),
                             pfrom->id);
                }
            } else {
                if (fBlocksOnly)
                    LogPrint("net", "transaction (%s) inv sent in violation of protocol peer=%d\n", inv.hash.ToString(),
                             pfrom->id);
                else if (!fAlreadyHave && !fImporting && !fReindexing)
                    pfrom->AskFor(inv);
            }

            // Track requests for our stuff
            GetMainSignals().Inventory(inv.hash);

            if (pfrom->nSendSize > (SendBufferSize() * 2)) {
                Misbehaving(pfrom->GetId(), 50);
                return error("send buffer size() = %u", pfrom->nSendSize);
            }
        }

        if (!vToFetch.empty())
            pfrom->PushMessage(NetMsgType::GETDATA, vToFetch);
        return true;
    }
}