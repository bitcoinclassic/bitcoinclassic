// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_CONSENSUS_H
#define BITCOIN_CONSENSUS_CONSENSUS_H

static const uint64_t TWENTY_MEG_FORK_TIME = 1442016000; // 12 September 2015 00:00:00 UTC

/** The maximum allowed size for a serialized block, in bytes (network rule) */
inline unsigned int MaxBlockSize(uint64_t nBlockTimestamp) {
    // 1MB blocks until 12 September 2015, then 2MB
    return (nBlockTimestamp < TWENTY_MEG_FORK_TIME ? 1000*1000 : 2*1000*1000);
}

/** The maximum allowed size for a serialized transaction, in bytes */
static const unsigned int MAX_TRANSACTION_SIZE = 1000*1000;

/** The maximum allowed number of signature check operations in a block (network rule) */
inline unsigned int MaxBlockSigops(uint64_t nBlockTimestamp) {
    return MaxBlockSize(nBlockTimestamp)/50;
}

/** Coinbase transaction outputs can only be spent after this number of new blocks (network rule) */
static const int COINBASE_MATURITY = 100;
/** Threshold for nLockTime: below this value it is interpreted as block number, otherwise as UNIX timestamp. */
static const unsigned int LOCKTIME_THRESHOLD = 500000000; // Tue Nov  5 00:53:20 1985 UTC

#endif // BITCOIN_CONSENSUS_CONSENSUS_H
