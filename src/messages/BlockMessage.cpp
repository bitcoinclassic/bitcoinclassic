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

#include "BlockMessage.h"

namespace Network {
    bool BlockMessage::handle(CNode *const pfrom,
                              CDataStream &vRecv,
                              int64_t nTimeReceived,
                              std::string &strCommand,
                              const bool xthinEnabled,
                              const bool fReindexing) {
        CBlock block;
        try {
            vRecv >> block;
        } catch (std::exception &e) {
            LogPrint("net", "ProcessMessage/block failed to parse message and got error: %s\n", e.what());
            pfrom->fDisconnect = true;
            return true;
        }

        CInv inv(MSG_BLOCK, block.GetHash());
        logDebug(Log::Net) << "received block" << inv << "peer" << pfrom->id;

        pfrom->AddInventoryKnown(inv);

        // BUIP010 Extreme Thinblocks: Handle Block Message
        HandleBlockMessage(pfrom, strCommand, block, inv);
        std::vector<uint256> orphans;
        orphans.reserve(block.vtx.size());
        for (unsigned int i = 0; i < block.vtx.size(); i++) {
            orphans.push_back(block.vtx[i].GetHash());
        }
        CTxOrphanCache::instance()->EraseOrphans(orphans);
        return true;
    }
}