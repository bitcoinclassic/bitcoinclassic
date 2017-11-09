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
#include "XThinBlockMessage.h"

namespace Network {
    bool XThinBlockMessage::handle(CNode *const pfrom,
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
        CXThinBlock thinBlock;
        vRecv >> thinBlock;

        // Send expedited ASAP
        CValidationState state;
        if (!CheckBlockHeader(thinBlock.header, state, true)) { // block header is bad
            LogPrint("thin", "Thinblock %s received with bad header from peer %s (%d)\n",
                     thinBlock.header.GetHash().ToString(), pfrom->addrName.c_str(), pfrom->id);
            misbehaving(pfrom->id, 20);
            return false;
        } else if (!IsRecentlyExpeditedAndStore(thinBlock.header.GetHash()))
            SendExpeditedBlock(thinBlock, 0, pfrom);

        CInv inv(MSG_BLOCK, thinBlock.header.GetHash());
#ifdef LOG_XTHINBLOCKS
        int nSizeThinBlock = ::GetSerializeSize(thinBlock, SER_NETWORK, PROTOCOL_VERSION);
        LogPrint("thin", "Received thinblock %s from peer %s (%d). Size %d bytes.\n", inv.hash.ToString(), pfrom->addrName.c_str(), pfrom->id, nSizeThinBlock);
#endif

        bool fAlreadyHave = false;
        // An expedited block or re-requested xthin can arrive and beat the original thin block request/response
        if (!pfrom->mapThinBlocksInFlight.count(inv.hash)) {
            LogPrint("thin", "Thinblock %s from peer %s (%d) received but we already have it\n", inv.hash.ToString(),
                     pfrom->addrName.c_str(), pfrom->id);
            LOCK(cs_main);
            fAlreadyHave = alreadyHave(inv); // I'll still continue processing if we don't have an accepted block yet
        }

        if (!fAlreadyHave) {
            if (thinBlock.process(pfrom))
                HandleBlockMessage(pfrom, strCommand, pfrom->thinBlock, thinBlock.GetInv());  // clears the thin block
        }

        return true;
    }
}