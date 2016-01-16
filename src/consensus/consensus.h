// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_CONSENSUS_H
#define BITCOIN_CONSENSUS_CONSENSUS_H

static const uint64_t TWO_MEG_FORK_TIME = 1456790400; // 1 March 2016 00:00:00 UTC

/** The maximum allowed size for a serialized block, in bytes (network rule) */
inline unsigned int MaxBlockSize(uint64_t nBlockTimestamp) {
    // 1MB blocks until 1 March 2016, then 2MB
    return (nBlockTimestamp < TWO_MEG_FORK_TIME ? 1000*1000 : 2*1000*1000);
}

/** The maximum allowed size for a serialized transaction, in bytes */
static const unsigned int MAX_TRANSACTION_SIZE = 1000*1000;

/** The maximum allowed number of signature check operations in a block (network rule) */
inline unsigned int MaxBlockSigops(uint64_t nBlockTimestamp) {
    return MaxBlockSize(nBlockTimestamp)/50;
}

/** Coinbase transaction outputs can only be spent after this number of new blocks (network rule) */
static const int COINBASE_MATURITY = 100;

/** Flags for LockTime() */
enum {
    /* Use GetMedianTimePast() instead of nTime for end point timestamp. */
    LOCKTIME_MEDIAN_TIME_PAST = (1 << 1),
};

/** Used as the flags parameter to CheckFinalTx() in non-consensus code */
static const unsigned int STANDARD_LOCKTIME_VERIFY_FLAGS = LOCKTIME_MEDIAN_TIME_PAST;

#endif // BITCOIN_CONSENSUS_CONSENSUS_H
