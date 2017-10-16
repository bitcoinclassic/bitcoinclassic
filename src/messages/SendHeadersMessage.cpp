//
// Created by gubatron on 10/14/17.
//

#include "SendHeadersMessage.h"

namespace Network {

    bool SendHeadersMessage::handle(CNode *const pfrom,
                                    CDataStream &vRecv,
                                    int64_t nTimeReceived,
                                    std::string &strCommand,
                                    const bool xthinEnabled,
                                    const bool fReindexing)
    {
        LOCK(cs_main);
        // BUIP010 Xtreme Thinblocks: We only do inv/getdata for xthinblocks and so we must have headersfirst turned off
        State(pfrom->GetId())->fPreferHeaders = !xthinEnabled;
        return true;
    }
}
