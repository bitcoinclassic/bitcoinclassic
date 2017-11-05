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

namespace Network {

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

    bool acceptBlockHeader(const CBlockHeader &block,
                           CValidationState &state,
                           const CChainParams &chainparams,
                           CBlockIndex **ppindex = nullptr);

    bool alreadyHave(const CInv &inv) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    bool canDirectFetch(const Consensus::Params &consensusParams);

    void findNextBlocksToDownload(NodeId nodeid, unsigned int count, std::vector<CBlockIndex *> &vBlocks,
                                  NodeId &nodeStaller);

    void markBlockAsInFlight(NodeId nodeid, const uint256 &hash, const Consensus::Params &consensusParams,
                             CBlockIndex *pindex = nullptr);

    bool markBlockAsReceived(const uint256 &hash);

    void misbehaving(NodeId nodeId, int howmuch);

    bool peerHasHeader(CNodeState *state, CBlockIndex *pindex);

    void processBlockAvailability(NodeId nodeid);

    void processGetData(CNode *pfrom, const Consensus::Params &consensusParams);

    void updateBlockAvailability(NodeId nodeid, const uint256 &hash);
}
#endif //BITCOINCLASSIC_NETWORKMESSAGE_H