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

#include "TxMessage.h"

namespace {
    bool TxMessage::handle(CNode *const pfrom,
                           CDataStream &vRecv,
                           int64_t nTimeReceived,
                           std::string &strCommand,
                           const bool xthinEnabled,
                           const bool fReindexing)
    {
        // Stop processing the transaction early if
        // We are in blocks only mode and peer is either not whitelisted or whitelistrelay is off
        if (GetBoolArg("-blocksonly", DEFAULT_BLOCKSONLY) &&
            (!pfrom->fWhitelisted || !GetBoolArg("-whitelistrelay", DEFAULT_WHITELISTRELAY))) {
            LogPrint("net", "transaction sent in violation of protocol peer=%d\n", pfrom->id);
            return true;
        }

        std::vector<uint256> vWorkQueue;
        std::vector<uint256> vEraseQueue;
        CTransaction tx;
        vRecv >> tx;

        CInv inv(MSG_TX, tx.GetHash());
        pfrom->AddInventoryKnown(inv);

        LOCK(cs_main);

        bool fMissingInputs = false;
        CValidationState state;

        pfrom->setAskFor.erase(inv.hash);
        mapAlreadyAskedFor.erase(inv.hash);

        if (!AlreadyHave(inv) && AcceptToMemoryPool(mempool, state, tx, true, &fMissingInputs)) {
            mempool.check(pcoinsTip);
            RelayTransaction(tx);
            vWorkQueue.push_back(inv.hash);

            logDebug(Log::Mempool) << "AcceptToMemoryPool: peer" << pfrom->id << "accepted" << tx.GetHash()
                                   << "(poolsz" << mempool.size() << "txn," << (mempool.DynamicMemoryUsage() / 1000)
                                   << "kB)";

            // Recursively process any orphan transactions that depended on this one
            std::set<NodeId> setMisbehaving;
            for (unsigned int i = 0; i < vWorkQueue.size(); i++) {
                auto orphans = CTxOrphanCache::instance()->fetchTransactionsByPrev(vWorkQueue[i]);
                for (auto mi = orphans.begin(); mi != orphans.end(); ++mi) {
                    const CTransaction &orphanTx = mi->tx;
                    const uint256 orphanHash = orphanTx.GetHash();
                    bool fMissingInputs2 = false;
                    // Use a dummy CValidationState so someone can't setup nodes to counter-DoS based on orphan
                    // resolution (that is, feeding people an invalid transaction based on LegitTxX in order to get
                    // anyone relaying LegitTxX banned)
                    CValidationState stateDummy;

                    if (setMisbehaving.count(mi->fromPeer))
                        continue;
                    if (AcceptToMemoryPool(mempool, stateDummy, orphanTx, true, &fMissingInputs2)) {
                        logDebug(Log::Mempool) << "   accepted orphan tx" << orphanHash;
                        RelayTransaction(orphanTx);
                        vWorkQueue.push_back(orphanHash);
                        vEraseQueue.push_back(orphanHash);
                    } else if (!fMissingInputs2) {
                        int nDos = 0;
                        if (stateDummy.IsInvalid(nDos) && nDos > 0) {
                            // Punish peer that gave us an invalid orphan tx
                            Misbehaving(mi->fromPeer, nDos);
                            setMisbehaving.insert(mi->fromPeer);
                            logDebug(Log::Mempool) << "   invalid orphan tx" << orphanHash;
                        }
                        // Has inputs but not accepted to mempool
                        // Probably non-standard or insufficient fee/priority
                        logDebug(Log::Mempool) << "   removed orphan tx" << orphanHash;
                        vEraseQueue.push_back(orphanHash);
                        assert(recentRejects);
                        recentRejects->insert(orphanHash);
                    }
                    mempool.check(pcoinsTip);
                }
            }

            CTxOrphanCache::instance()->EraseOrphans(vEraseQueue);
            CTxOrphanCache::instance()->EraseOrphansByTime();
        } else if (fMissingInputs) {
            CTxOrphanCache *cache = CTxOrphanCache::instance();
            // DoS prevention: do not allow CTxOrphanCache to grow unbounded
            cache->AddOrphanTx(tx, pfrom->GetId());
            std::uint32_t nEvicted = cache->LimitOrphanTxSize();
            if (nEvicted > 0)
                logDebug(Log::Mempool) << "mapOrphan overflow, removed" << nEvicted << "tx";
        } else {
            assert(recentRejects);
            recentRejects->insert(tx.GetHash());

            if (pfrom->fWhitelisted && GetBoolArg("-whitelistforcerelay", DEFAULT_WHITELISTFORCERELAY)) {
                // Always relay transactions received from whitelisted peers, even
                // if they were already in the mempool or rejected from it due
                // to policy, allowing the node to function as a gateway for
                // nodes hidden behind it.
                //
                // Never relay transactions that we would assign a non-zero DoS
                // score for, as we expect peers to do the same with us in that
                // case.
                int nDoS = 0;
                if (!state.IsInvalid(nDoS) || nDoS == 0) {
                    LogPrintf("Force relaying tx %s from whitelisted peer=%d\n", tx.GetHash().ToString(), pfrom->id);
                    RelayTransaction(tx);
                } else {
                    LogPrintf("Not relaying invalid transaction %s from whitelisted peer=%d (%s)\n",
                              tx.GetHash().ToString(), pfrom->id, FormatStateMessage(state));
                }
            }
        }
        int nDoS = 0;
        if (state.IsInvalid(nDoS)) {
            logWarning(Log::Mempool) << tx.GetHash() << "from peer" << pfrom->id << "was not accepted"
                                     << FormatStateMessage(state);
            if (state.GetRejectCode() < REJECT_INTERNAL) // Never send AcceptToMemoryPool's internal codes over P2P
                pfrom->PushMessage(NetMsgType::REJECT, strCommand, (unsigned char) state.GetRejectCode(),
                                   state.GetRejectReason().substr(0, MAX_REJECT_MESSAGE_LENGTH), inv.hash);
            if (nDoS > 0)
                Misbehaving(pfrom->GetId(), nDoS);
        }
        FlushStateToDisk(state, FLUSH_STATE_PERIODIC);

        return true;
    }
}