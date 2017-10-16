//
// Created by gubatron on 10/14/17.
//

#ifndef BITCOINCLASSIC_SENDHEADERSMESSAGE_H
#define BITCOINCLASSIC_SENDHEADERSMESSAGE_H

#include "NetworkMessage.h"

namespace Network {
    class SendHeadersMessage :
            public NetworkMessage {
    public:
        SendHeadersMessage() : NetworkMessage() {}

        bool handle(CNode *const pfrom,
                    CDataStream &vRecv,
                    int64_t nTimeReceived,
                    std::string &strCommand,
                    const bool xthinEnabled,
                    const bool fReindexing);
    };
}
#endif //BITCOINCLASSIC_SENDHEADERSMESSAGE_H