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

#include "VerAckMessage.h"

namespace {

    CNodeState *State(NodeId pnode);

    bool VerAckMessage::handle(CNode *const pfrom,
                               CDataStream &vRecv,
                               int64_t nTimeReceived,
                               std::string &strCommand,
                               const bool xthinEnabled,
                               const bool fReindexing)
    {
        pfrom->SetRecvVersion(std::min(pfrom->nVersion, PROTOCOL_VERSION));

        // Mark this node as currently connected, so we update its timestamp later.
        if (pfrom->fNetworkNode) {
            LOCK(cs_main);
            State(pfrom->GetId())->fCurrentlyConnected = true;
        }

        if (pfrom->nVersion >= SENDHEADERS_VERSION) {
            // Tell our peer we prefer to receive headers rather than inv's
            // We send this to non-NODE NETWORK peers as well, because even
            // non-NODE NETWORK peers can announce blocks (such as pruning
            // nodes)

            // BUIP010 Extreme Thinblocks: We only do inv/getdata for xthinblocks and so we must have headersfirst turned off
            if (!xthinEnabled)
                pfrom->PushMessage(NetMsgType::SENDHEADERS);
        }

        // send our listening port in a separate version message
        if (pfrom->nVersion >= EXPEDITED_VERSION)
            pfrom->PushMessage(NetMsgType::VERSION2, GetListenPort());

        return true;
    }
}