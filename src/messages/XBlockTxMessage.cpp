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

#include "XBlockTxMessage.h"

namespace Network {
    bool XBlockTxMessage::handle(CNode *const pfrom,
                                 CDataStream &vRecv,
                                 int64_t nTimeReceived,
                                 std::string &strCommand,
                                 const bool xthinEnabled,
                                 const bool fReindexing)
    {
        if (!xthinEnabled) {
            LOCK(cs_main);
            misbehaving(pfrom->GetId(), 100);
            return false;
        }
        if (pfrom->xThinBlockHashes.size() != pfrom->thinBlock.vtx.size()) { // crappy, but fast solution.
            LogPrint("thin", "Inconsistent thin block data while processing xblock-tx\n");
            return true;
        }

        CXThinBlockTx thinBlockTx;
        vRecv >> thinBlockTx;

        CInv inv(MSG_XTHINBLOCK, thinBlockTx.blockhash);
        logDebug(Log::Net) << "received blocktxs for" << inv.hash << "peer" << pfrom->id;
        if (!pfrom->mapThinBlocksInFlight.count(inv.hash)) {
            LogPrint("thin",
                     "ThinblockTx received but it was either not requested or it was beaten by another block %s  peer=%d\n",
                     inv.hash.ToString(), pfrom->id);
            return true;
        }

        // Create the mapMissingTx from all the supplied tx's in the xthinblock
        std::map<uint64_t, CTransaction> mapMissingTx;
        BOOST_FOREACH(CTransaction tx, thinBlockTx.vMissingTx) {
            mapMissingTx[tx.GetHash().GetCheapHash()] = tx;
        }

        int count = 0;
        for (size_t i = 0; i < pfrom->thinBlock.vtx.size(); ++i) {
            if (pfrom->thinBlock.vtx[i].IsNull()) {
                auto val = mapMissingTx.find(pfrom->xThinBlockHashes[i]);
                if (val != mapMissingTx.end()) {
                    pfrom->thinBlock.vtx[i] = val->second;
                    --pfrom->thinBlockWaitingForTxns;
                }
                count++;
            }
        }
        LogPrint("thin", "Got %d Re-requested txs, needed %d of them\n", thinBlockTx.vMissingTx.size(), count);

        if (pfrom->thinBlockWaitingForTxns == 0) {
            // We have all the transactions now that are in this block: try to reassemble and process.
            pfrom->thinBlockWaitingForTxns = -1;
            pfrom->AddInventoryKnown(inv);

#ifdef LOG_XTHINBLOCKS
            // for compression statistics, we have to add up the size of xthinblock and the re-requested thinBlockTx.
            int nSizeThinBlockTx = ::GetSerializeSize(thinBlockTx, SER_NETWORK, PROTOCOL_VERSION);
            int blockSize = pfrom->thinBlock.GetSerializeSize(SER_NETWORK, CBlock::CURRENT_VERSION);
            LogPrint("thin", "Reassembled thin block for %s (%d bytes). Message was %d bytes (thinblock) and %d bytes (re-requested tx), compression ratio %3.2f\n",
                     pfrom->thinBlock.GetHash().ToString(),
                     blockSize,
                     pfrom->nSizeThinBlock,
                     nSizeThinBlockTx,
                     ((float) blockSize) / ( (float) pfrom->nSizeThinBlock + (float) nSizeThinBlockTx )
                     );
#endif

            // For correctness sake, assume all came from the orphans cache
            std::vector<uint256> orphans;
            orphans.reserve(pfrom->thinBlock.vtx.size());
            for (unsigned int i = 0; i < pfrom->thinBlock.vtx.size(); i++) {
                orphans.push_back(pfrom->thinBlock.vtx[i].GetHash());
            }
            HandleBlockMessage(pfrom, strCommand, pfrom->thinBlock, inv);
            CTxOrphanCache::instance()->EraseOrphans(orphans);
        } else {
            LogPrint("thin", "Failed to retrieve all transactions for block\n");
        }
        return true;
    }
}