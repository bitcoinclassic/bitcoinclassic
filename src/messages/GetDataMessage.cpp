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

#include "GetDataMessage.h"

namespace Network {
    bool GetDataMessage::handle(CNode *const pfrom,
                                CDataStream &vRecv,
                                int64_t nTimeReceived,
                                std::string &strCommand,
                                const bool xthinEnabled,
                                const bool fReindexing)
    {
        std::vector<CInv> vInv;
        vRecv >> vInv;
        if (vInv.size() > MAX_INV_SZ) {
            misbehaving(pfrom->GetId(), 20);
            return error("message getdata size() = %u", vInv.size());
        }

        if (fDebug || (vInv.size() != 1))
            logDebug(Log::Net) << "received getdata (" << vInv.size() << "invsz) peer:" << pfrom->id;

        if ((fDebug && vInv.size() > 0) || (vInv.size() == 1))
            logDebug(Log::Net) << "received getdata for:" << vInv[0].ToString() << "peer:" << pfrom->id;

        pfrom->vRecvGetData.insert(pfrom->vRecvGetData.end(), vInv.begin(), vInv.end());
        processGetData(pfrom, Params().GetConsensus());
        return true;
    }
}