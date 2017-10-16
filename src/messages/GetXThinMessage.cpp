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

#include "GetXThinMessage.h"

namespace Network {
    bool GetXThinMessage::handle(CNode *const pfrom,
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
        CBloomFilter filterMemPool;
        CInv inv;
        vRecv >> inv >> filterMemPool;
        if (inv.type != MSG_XTHINBLOCK && inv.type != MSG_THINBLOCK) {
            LOCK(cs_main);
            Misbehaving(pfrom->GetId(), 20);
            return false;
        }

        LoadFilter(pfrom, &filterMemPool);
        pfrom->vRecvGetData.insert(pfrom->vRecvGetData.end(), inv);
        ProcessGetData(pfrom, Params().GetConsensus());
    }
}