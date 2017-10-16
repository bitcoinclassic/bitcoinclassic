//
// Created by gubatron on 10/14/17.
//

#ifndef BITCOINCLASSIC_GETXTHINMESSAGE_H
#define BITCOINCLASSIC_GETXTHINMESSAGE_H

#include "NetworkMessage.h"

namespace {
    class GetXThinMessage : public NetworkMessage {
    public:
        ~GetXThinMessage() {}

        GetXThinMessage() : NetworkMessage() {}

        bool handle(CNode *const pfrom,
                    CDataStream &vRecv,
                    int64_t nTimeReceived,
                    std::string &strCommand,
                    const bool xthinEnabled,
                    const bool fReindexing);
    };
}
#endif //BITCOINCLASSIC_GETXTHINMESSAGE_H