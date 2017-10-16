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


#ifndef BITCOINCLASSIC_EXPEDITEDREQUESTMESSAGE_H
#define BITCOINCLASSIC_EXPEDITEDREQUESTMESSAGE_H

#include "NetworkMessage.h"

namespace Network {

    class ExpeditedRequestMessage : public NetworkMessage {
    public:
        ExpeditedRequestMessage() : NetworkMessage() {}

        bool handle(CNode *const pfrom,
                    CDataStream &vRecv,
                    int64_t nTimeReceived,
                    std::string &strCommand,
                    const bool xthinEnabled,
                    const bool fReindexing);
    };
}
#endif //BITCOINCLASSIC_EXPEDITEDREQUESTMESSAGE_H