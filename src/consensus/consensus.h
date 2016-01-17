// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_CONSENSUS_H
#define BITCOIN_CONSENSUS_CONSENSUS_H

#include <stdint.h>

static const uint64_t BCIP103_FORK_TIME = 1456790400; // April 01 2016, midnight UTC

/** The maximum allowed size for a serialized block, in bytes (network rule) */
inline unsigned int MaxBlockSize(uint64_t nTime) {
    if (nTime < BCIP01_FORK_TIME)
        return 1000*1000;

    // cap for tests
    if (nTime > 4113158400)
        nTime = 4113158400;

/** Smooth transition of 30 bytes every 10 minutes, until 3 megabytes is reached */
    if (((2*1000*1000) + (30 * ((nTime - BCIP01_FORK_TIME) / 600))) <= 3000000) {
      return (2*1000*1000) + (30 * ((nTime - BCIP01_FORK_TIME) / 600));
    }
    if (((2*1000*1000) + (30 * ((nTime - BCIP01_FORK_TIME) / 600))) > 3000000){
      return 4000000;
    }
}

/** The maximum allowed size for a serialized transaction, in bytes */
static const unsigned int MAX_TRANSACTION_SIZE = 1000*1000;

/** The maximum allowed number of signature check operations in a block (network rule) */
inline unsigned int MaxBlockSigops(uint64_t nTime) {
    return MaxBlockSize(nTime) / 50;
}

/** Coinbase transaction outputs can only be spent after this number of new blocks (network rule) */
static const int COINBASE_MATURITY = 100;

/** Flags for LockTime() */
enum {
    /* Use GetMedianTimePast() instead of nTime for end point timestamp. */
    LOCKTIME_MEDIAN_TIME_PAST = (1 << 1),
};

#endif // BITCOIN_CONSENSUS_CONSENSUS_H
