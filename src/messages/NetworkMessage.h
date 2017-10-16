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

#ifndef BITCOINCLASSIC_NETWORKMESSAGE_H
#define BITCOINCLASSIC_NETWORKMESSAGE_H

#include <main.h>
#include <consensus/validation.h>
#include <net.h>
#include <chainparams.h>
#include <timedata.h>
#include <addrman.h>
#include <txorphancache.h>
#include <txmempool.h>
#include <merkleblock.h>
#include <thinblock.h>
#include <validationinterface.h>


static bool CheckIndexAgainstCheckpoint(const CBlockIndex* pindexPrev, CValidationState& state, const CChainParams& chainparams, const uint256& hash);

namespace Network {

    CNodeState *State(NodeId pnode);

    class NetworkMessage {
    public:
        virtual ~NetworkMessage() {}

        NetworkMessage() {}

        virtual bool handle(CNode *const pfrom,
                            CDataStream &vRecv,
                            int64_t nTimeReceived,
                            std::string &strCommand,
                            const bool xthinEnabled,
                            const bool fReindexing) = 0;

        friend class CAddrMan;
    };

    bool AcceptBlockHeader(const CBlockHeader &block,
                           CValidationState &state,
                           const CChainParams &chainparams,
                           CBlockIndex **ppindex = NULL);

    bool AlreadyHave(const CInv &inv) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    bool CanDirectFetch(const Consensus::Params &consensusParams);

    void ProcessBlockAvailability(NodeId nodeid);

    void ProcessGetData(CNode *pfrom, const Consensus::Params &consensusParams);

    void UpdateBlockAvailability(NodeId nodeid, const uint256 &hash);
}
#endif //BITCOINCLASSIC_NETWORKMESSAGE_H