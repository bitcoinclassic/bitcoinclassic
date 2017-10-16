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

#include "GetXBlockTxMessage.h"

namespace Network {
    bool GetXBlockTxMessage::handle(CNode *const pfrom,
                                    CDataStream &vRecv,
                                    int64_t nTimeReceived,
                                    std::string &strCommand,
                                    const bool xthinEnabled,
                                    const bool fReindexing)
    {
        if (!xthinEnabled) {
            LOCK(cs_main);
            Misbehaving(pfrom->GetId(), 100);
            return false;
        }
        CXRequestThinBlockTx thinRequestBlockTx;
        vRecv >> thinRequestBlockTx;

        if (thinRequestBlockTx.setCheapHashesToRequest.empty()) { // empty request??
            LOCK(cs_main);
            Misbehaving(pfrom->GetId(), 100);
            return false;
        }
        // We use MSG_TX here even though we refer to blockhash because we need to track
        // how many xblocktx requests we make in case of DOS
        CInv inv(MSG_TX, thinRequestBlockTx.blockhash);
        LogPrint("thin", "received get_xblocktx for %s peer=%d\n", inv.hash.ToString(), pfrom->id);

        // Check for Misbehaving and DOS
        // If they make more than 20 requests in 10 minutes then disconnect them
        {
            const uint64_t nNow = GetTime();
            if (pfrom->nGetXBlockTxLastTime <= 0)
                pfrom->nGetXBlockTxLastTime = nNow;
            pfrom->nGetXBlockTxCount *= pow(1.0 - 1.0 / 600.0, (double) (nNow - pfrom->nGetXBlockTxLastTime));
            pfrom->nGetXBlockTxLastTime = nNow;
            pfrom->nGetXBlockTxCount += 1;
            LogPrint("thin", "nGetXBlockTxCount is %f\n", pfrom->nGetXBlockTxCount);
            if (pfrom->nGetXBlockTxCount >= 20) {
                LogPrintf("DOS: Misbehaving - requesting too many xblocktx: %s\n", inv.hash.ToString());
                LOCK(cs_main);
                Misbehaving(pfrom->GetId(), 100);  // If they exceed the limit then disconnect them
            }
        }

        LOCK(cs_main);
        auto mi = Blocks::indexMap.find(inv.hash);
        if (mi == Blocks::indexMap.end()) {
            Misbehaving(pfrom->GetId(), 100);
            return false;
        }
        CBlockIndex *index = mi->second;
        assert(index);
        if (index->nHeight + 100 < chainActive.Height()) {
            // a node that is behind should never use this method.
            Misbehaving(pfrom->GetId(), 10);
            return false;
        }
        if ((index->nStatus & BLOCK_HAVE_DATA) == 0) {
            LogPrintf("GET_XBLOCKTX requested block-data not available %s\n", inv.hash.ToString().c_str());
            return false;
        }
        CBlock block;
        const Consensus::Params &consensusParams = Params().GetConsensus();
        if (!ReadBlockFromDisk(block, index, consensusParams)) {
            LogPrintf("Internal error, file missing datafile %d (block: %d)\n", index->nFile, index->nHeight);
            return false;
        }

        std::vector<CTransaction> vTx;
        int todo = thinRequestBlockTx.setCheapHashesToRequest.size();
        for (size_t i = 1; i < block.vtx.size(); i++) {
            uint64_t cheapHash = block.vtx[i].GetHash().GetCheapHash();
            if (thinRequestBlockTx.setCheapHashesToRequest.count(cheapHash)) {
                vTx.push_back(block.vtx[i]);
                if (--todo == 0)
                    break;
            }
        }
        if (todo > 0) { // node send us a request for transactions which were not in the block.
            Misbehaving(pfrom->GetId(), 100);
            return false;
        }

        pfrom->AddInventoryKnown(inv);
        CXThinBlockTx thinBlockTx(thinRequestBlockTx.blockhash, vTx);
        pfrom->PushMessage(NetMsgType::XBLOCKTX, thinBlockTx);

        return true;
    }
}