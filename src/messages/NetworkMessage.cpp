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

#include "NetworkMessage.h"

namespace Network {
    // Used in InvMesssage, TxMessage and XThinBlockMessage
    bool AlreadyHave(const CInv &inv) EXCLUSIVE_LOCKS_REQUIRED(cs_main) {
    switch (inv.type) {
        case MSG_TX: {
            assert(recentRejects);
            if (chainActive.Tip()->GetBlockHash() != hashRecentRejectsChainTip) {
                // If the chain tip has changed previously rejected transactions
                // might be now valid, e.g. due to a nLockTime'd tx becoming valid,
                // or a double-spend. Reset the rejects filter and give those
                // txs a second chance.
                hashRecentRejectsChainTip = chainActive.Tip()->GetBlockHash();
                recentRejects->reset();
            }

            return recentRejects->contains(inv.hash) ||
                   mempool.exists(inv.hash) ||
                   CTxOrphanCache::contains(inv.hash) ||
                   pcoinsTip->HaveCoins(inv.hash);
        }
        case MSG_BLOCK:
            return Blocks::indexMap.count(inv.hash);
    }
    // Don't know what it is, just say we already got one
    return true;
    }

    // Used in main.cpp (FindNextBlocksToDownload, SendMessages) and below on UpdateBlockAvailability
    /** Check whether the last unknown block a peer advertised is not yet known. */
    void ProcessBlockAvailability(NodeId nodeid) {
        CNodeState *state = State(nodeid);
        assert(state != NULL);

        if (!state->hashLastUnknownBlock.IsNull()) {
            auto itOld = Blocks::indexMap.find(state->hashLastUnknownBlock);
            if (itOld != Blocks::indexMap.end() && itOld->second->nChainWork > 0) {
                if (state->pindexBestKnownBlock == NULL ||
                    itOld->second->nChainWork >= state->pindexBestKnownBlock->nChainWork)
                    state->pindexBestKnownBlock = itOld->second;
                state->hashLastUnknownBlock.SetNull();
            }
        }
    }

    // Used in main.cpp::ProcessMessages, GetDataMessage, GetXThinMessage
    void ProcessGetData(CNode *pfrom, const Consensus::Params &consensusParams) {
        std::deque<CInv>::iterator it = pfrom->vRecvGetData.begin();

        std::vector<CInv> vNotFound;

        LOCK(cs_main);

        while (it != pfrom->vRecvGetData.end()) {
            // Don't bother if send buffer is too full to respond anyway
            if (pfrom->nSendSize >= SendBufferSize())
                break;

            const CInv &inv = *it;
            {
                boost::this_thread::interruption_point();
                it++;

                if (inv.type == MSG_BLOCK || inv.type == MSG_FILTERED_BLOCK
                    || inv.type == MSG_THINBLOCK || inv.type == MSG_XTHINBLOCK) {
                    bool send = false;
                    auto mi = Blocks::indexMap.find(inv.hash);
                    if (mi != Blocks::indexMap.end()) {
                        if (chainActive.Contains(mi->second)) {
                            send = true;
                        } else {
                            static const int nOneMonth = 30 * 24 * 60 * 60;
                            // To prevent fingerprinting attacks, only send blocks outside of the active
                            // chain if they are valid, and no more than a month older (both in time, and in
                            // best equivalent proof of work) than the best header chain we know about.
                            send = mi->second->IsValid(BLOCK_VALID_SCRIPTS) && (pindexBestHeader != NULL) &&
                                   (pindexBestHeader->GetBlockTime() - mi->second->GetBlockTime() < nOneMonth) &&
                                   (GetBlockProofEquivalentTime(*pindexBestHeader, *mi->second, *pindexBestHeader,
                                                                consensusParams) < nOneMonth);
                            if (!send) {
                                LogPrintf(
                                "%s: ignoring request from peer=%i for old block that isn't in the main chain\n",
                                __func__, pfrom->GetId());
                            }
                        }
                    }
                    // disconnect node in case we have reached the outbound limit for serving historical blocks
                    // never disconnect whitelisted nodes
                    static const int nOneWeek = 7 * 24 * 60 * 60; // assume > 1 week = historical
                    if (send && CNode::OutboundTargetReached(true) && (((pindexBestHeader != NULL) &&
                                                                        (pindexBestHeader->GetBlockTime() -
                                                                         mi->second->GetBlockTime() > nOneWeek)) ||
                                                                       inv.type == MSG_FILTERED_BLOCK) &&
                        !pfrom->fWhitelisted) {
                        LogPrint("net", "historical block serving limit reached, disconnect peer=%d\n", pfrom->GetId());

                        //disconnect node
                        pfrom->fDisconnect = true;
                        send = false;
                    }
                    // Pruned nodes may have deleted the block, so check whether
                    // it's available before trying to send.
                    if (send && (mi->second->nStatus & BLOCK_HAVE_DATA)) {
                        // Send block from disk
                        CBlock block;
                        if (!ReadBlockFromDisk(block, (*mi).second, consensusParams))
                            assert(!"cannot load block from disk");

                        bool sendFullBlock = true;

                        if (inv.type == MSG_XTHINBLOCK) {
                            CXThinBlock xThinBlock(block, pfrom->pThinBlockFilter);
                            if (!xThinBlock.collision) {
                                const int nSizeBlock = ::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION);
                                // Only send a thinblock if smaller than a regular block
                                const int nSizeThinBlock = ::GetSerializeSize(xThinBlock, SER_NETWORK,
                                                                              PROTOCOL_VERSION);
                                if (nSizeThinBlock < nSizeBlock) {
                                    pfrom->PushMessage(NetMsgType::XTHINBLOCK, xThinBlock);
                                    sendFullBlock = false;
                                    LogPrint("thin",
                                             "Sent xthinblock - size: %d vs block size: %d => tx hashes: %d transactions: %d  peerid=%d\n",
                                             nSizeThinBlock, nSizeBlock, xThinBlock.vTxHashes.size(),
                                             xThinBlock.vMissingTx.size(), pfrom->id);
                                }
                            }
                        } else if (inv.type == MSG_FILTERED_BLOCK) {
                            LOCK(pfrom->cs_filter);
                            if (pfrom->pfilter) {
                                CMerkleBlock merkleBlock(block, *pfrom->pfilter);
                                pfrom->PushMessage(NetMsgType::MERKLEBLOCK, merkleBlock);
                                // CMerkleBlock just contains hashes, so also push any transactions in the block the client did not see
                                // This avoids hurting performance by pointlessly requiring a round-trip
                                // Note that there is currently no way for a node to request any single transactions we didn't send here -
                                // they must either disconnect and retry or request the full block.
                                // Thus, the protocol spec specified allows for us to provide duplicate txn here,
                                // however we MUST always provide at least what the remote peer needs
                                typedef std::pair<unsigned int, uint256> PairType;
                                BOOST_FOREACH(PairType &pair, merkleBlock.vMatchedTxn)
                                pfrom->PushMessage(NetMsgType::TX,
                                                   block.vtx[pair.first]);
                                sendFullBlock = false;
                            }
                        }
                        if (sendFullBlock) // if none of the other methods were actually executed;
                            pfrom->PushMessage(NetMsgType::BLOCK, block);

                        // Trigger the peer node to send a getblocks request for the next batch of inventory
                        if (inv.hash == pfrom->hashContinue) {
                            // Bypass PushInventory, this must send even if redundant,
                            // and we want it right after the last block so they don't
                            // wait for other stuff first.
                            std::vector<CInv> vInv;
                            vInv.push_back(CInv(MSG_BLOCK, chainActive.Tip()->GetBlockHash()));
                            pfrom->PushMessage(NetMsgType::INV, vInv);
                            pfrom->hashContinue.SetNull();
                        }
                    }
                } else if (inv.IsKnownType()) {
                    // Send stream from relay memory
                    bool pushed = false;
                    {
                        LOCK(cs_mapRelay);
                        std::map<CInv, CDataStream>::iterator mi = mapRelay.find(inv);
                        if (mi != mapRelay.end()) {
                            pfrom->PushMessage(inv.GetCommand(), (*mi).second);
                        }
                    }
                    if (!pushed && inv.type == MSG_TX) {
                        CTransaction tx;
                        if (mempool.lookup(inv.hash, tx)) {
                            CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
                            ss.reserve(1000);
                            ss << tx;
                            pfrom->PushMessage(NetMsgType::TX, ss);
                            pushed = true;
                        }
                    }
                    if (!pushed) {
                        vNotFound.push_back(inv);
                    }
                }

                // Track requests for our stuff.
                GetMainSignals().Inventory(inv.hash);

                if (inv.type == MSG_BLOCK || inv.type == MSG_FILTERED_BLOCK
                    || inv.type == MSG_THINBLOCK || inv.type == MSG_XTHINBLOCK)
                    break;
            }
        }

        pfrom->vRecvGetData.erase(pfrom->vRecvGetData.begin(), it);

        if (!vNotFound.empty()) {
            // Let the peer know that we didn't find what it asked for, so it doesn't
            // have to wait around forever. Currently only SPV clients actually care
            // about this message: it's needed when they are recursively walking the
            // dependencies of relevant unconfirmed transactions. SPV clients want to
            // do that because they want to know about (and store and rebroadcast and
            // risk analyze) the dependencies of transactions relevant to them, without
            // having to download the entire memory pool.
            pfrom->PushMessage(NetMsgType::NOTFOUND, vNotFound);
        }
    }

    // Used in main.cpp::AcceptBlock, HeadersMessage
    bool AcceptBlockHeader(const CBlockHeader &block,
                           CValidationState &state,
                           const CChainParams &chainparams,
                           CBlockIndex **ppindex = NULL) {
        AssertLockHeld(cs_main);
        // Check for duplicate
        uint256 hash = block.GetHash();
        auto miSelf = Blocks::indexMap.find(hash);
        CBlockIndex *pindex = NULL;
        if (hash != chainparams.GetConsensus().hashGenesisBlock) {

            if (miSelf != Blocks::indexMap.end()) {
                // Block header is already known.
                pindex = miSelf->second;
                if (ppindex)
                    *ppindex = pindex;
                if (pindex->nStatus & BLOCK_FAILED_MASK)
                    return state.Invalid(error("%s: block is marked invalid", __func__), 0, "duplicate");
                return true;
            }

            if (!CheckBlockHeader(block, state))
                return false;

            // Get prev block index
            CBlockIndex *pindexPrev = NULL;
            auto mi = Blocks::indexMap.find(block.hashPrevBlock);
            if (mi == Blocks::indexMap.end())
                return state.DoS(10, error("%s: prev block not found", __func__), 0, "bad-prevblk");
            pindexPrev = (*mi).second;
            if (pindexPrev->nStatus & BLOCK_FAILED_MASK)
                return state.DoS(100, error("%s: prev block invalid", __func__), REJECT_INVALID, "bad-prevblk");

            assert(pindexPrev);
            if (fCheckpointsEnabled && !CheckIndexAgainstCheckpoint(pindexPrev, state, chainparams, hash))
                return error("%s: CheckIndexAgainstCheckpoint(): %s", __func__, state.GetRejectReason().c_str());

            if (!ContextualCheckBlockHeader(block, state, pindexPrev))
                return false;
        }
        if (pindex == NULL)
            pindex = AddToBlockIndex(block);

        if (ppindex)
            *ppindex = pindex;

        return true;
    }
}

