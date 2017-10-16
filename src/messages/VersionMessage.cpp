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

#include "VersionMessage.h"

namespace {
    void UpdatePreferredDownload(CNode* node, CNodeState* state)
    {
        nPreferredDownload -= state->fPreferredDownload;

        // Whether this node should be marked as a preferred download node.
        state->fPreferredDownload = (!node->fInbound || node->fWhitelisted) && !node->fOneShot && !node->fClient;

        nPreferredDownload += state->fPreferredDownload;
    }

    bool VersionMessage::handle(CNode *const pfrom,
                                CDataStream &vRecv,
                                int64_t nTimeReceived,
                                std::string &strCommand,
                                const bool xthinEnabled,
                                const bool fReindexing)
    {
        // Each connection can only send one version message
        if (pfrom->nVersion != 0) {
            pfrom->PushMessage(NetMsgType::REJECT, strCommand, REJECT_DUPLICATE,
                               std::string("Duplicate version message"));
            Misbehaving(pfrom->GetId(), 1);
            return false;
        }

        int64_t nTime;
        CAddress addrMe;
        CAddress addrFrom;
        uint64_t nNonce = 1;
        vRecv >> pfrom->nVersion >> pfrom->nServices >> nTime >> addrMe;

        if (pfrom->nVersion < MIN_PEER_PROTO_VERSION) {
            // disconnect from peers older than this proto version
            LogPrintf("peer=%d using obsolete version %i; disconnecting\n", pfrom->id, pfrom->nVersion);
            pfrom->PushMessage(NetMsgType::REJECT, strCommand, REJECT_OBSOLETE,
                               strprintf("Version must be %d or greater", MIN_PEER_PROTO_VERSION));
            pfrom->fDisconnect = true;
            return false;
        }

        if (pfrom->nVersion == 10300)
            pfrom->nVersion = 300;
        if (!vRecv.empty())
            vRecv >> addrFrom >> nNonce;
        if (!vRecv.empty()) {
            vRecv >> LIMITED_STRING(pfrom->strSubVer, MAX_SUBVERSION_LENGTH);
            pfrom->cleanSubVer = SanitizeString(pfrom->strSubVer);
        }
        if (!vRecv.empty())
            vRecv >> pfrom->nStartingHeight;
        if (!vRecv.empty())
            vRecv >> pfrom->fRelayTxes; // set to true after we get the first filter* message
        else
            pfrom->fRelayTxes = true;

        // Disconnect if we connected to ourself
        if (nNonce == nLocalHostNonce && nNonce > 1) {
            LogPrintf("connected to self at %s, disconnecting\n", pfrom->addr.ToString());
            pfrom->fDisconnect = true;
            return true;
        }

        pfrom->addrLocal = addrMe;
        if (pfrom->fInbound && addrMe.IsRoutable()) {
            SeenLocal(addrMe);
        }

        // Be shy and don't send version until we hear
        if (pfrom->fInbound)
            pfrom->PushVersion();

        pfrom->fClient = !(pfrom->nServices & NODE_NETWORK);

        // Potentially mark this peer as a preferred download peer.
        UpdatePreferredDownload(pfrom, State(pfrom->GetId()));

        // Change version
        pfrom->PushMessage(NetMsgType::VERACK);
        pfrom->ssSend.SetVersion(std::min(pfrom->nVersion, PROTOCOL_VERSION));

        if (!pfrom->fInbound) {
            // Advertise our address
            if (fListen && !IsInitialBlockDownload()) {
                CAddress addr = GetLocalAddress(&pfrom->addr);
                if (addr.IsRoutable()) {
                    LogPrintf("ProcessMessages: advertising address %s\n", addr.ToString());
                    pfrom->PushAddress(addr);
                } else if (IsPeerAddrLocalGood(pfrom)) {
                    addr.SetIP(pfrom->addrLocal);
                    LogPrintf("ProcessMessages: advertising address %s\n", addr.ToString());
                    pfrom->PushAddress(addr);
                }
            }

            // Get recent addresses
            if (pfrom->fOneShot || pfrom->nVersion >= CADDR_TIME_VERSION || addrman.size() < 1000) {
                pfrom->PushMessage(NetMsgType::GETADDR);
                pfrom->fGetAddr = true;
            }
            addrman.Good(pfrom->addr);
        } else {
            if (((CNetAddr) pfrom->addr) == (CNetAddr) addrFrom) {
                addrman.Add(addrFrom, addrFrom);
                addrman.Good(addrFrom);
            }
        }

        pfrom->fSuccessfullyConnected = true;
        logCritical(Log::Net) << "receive version message:" << pfrom->addr << pfrom->cleanSubVer << "version:"
                              << pfrom->nVersion << "blocks:" << pfrom->nStartingHeight << "id:" << pfrom->id;
        if (pfrom->isCashNode)
            logInfo(Log::Net) << "peer id:" << pfrom->GetId() << "uses CASH message-headers";

        int64_t nTimeOffset = nTime - GetTime();
        pfrom->nTimeOffset = nTimeOffset;
        AddTimeData(pfrom->addr, nTimeOffset);

        return true;
    }

    bool Version2Message::handle(CNode *const pfrom,
                                 CDataStream &vRecv,
                                 int64_t nTimeReceived,
                                 std::string &strCommand,
                                 const bool xthinEnabled,
                                 const bool fReindexing)
    {
        // Each connection can only send one version message
        if (pfrom->addrFromPort != 0) {
            pfrom->PushMessage(NetMsgType::REJECT, strCommand, REJECT_DUPLICATE,
                               std::string("Duplicate version2 message"));
            LOCK(cs_main);
            Misbehaving(pfrom->GetId(), 15);
            return false;
        }

        vRecv >> pfrom->addrFromPort;
        pfrom->PushMessage(NetMsgType::VERACK2);
        return true;
    }

    bool
    VersionAck2Message::handle(CNode *const pfrom,
                               CDataStream &vRecv,
                               int64_t nTimeReceived,
                               std::string &strCommand,
                               const bool xthinEnabled,
                               const bool fReindexing)
    {
        CheckAndRequestExpeditedBlocks(pfrom);
        return true;
    }
}