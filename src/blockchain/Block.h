/*
 * This file is part of the bitcoin-classic project
 * Copyright (C) 2017 Tom Zander <tomz@freedommail.ch>
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


#ifndef BITCOIN_BLOCKCHAIN_BLOCK_H
#define BITCOIN_BLOCKCHAIN_BLOCK_H

#include "Transaction.h"
#include <streaming/ConstBuffer.h>

#include <uint256.h>
#include <vector>

class CBlock;
class CBlockHeader;

namespace Streaming {
    class BufferPool;
}

/**
 * @brief The FastBlock class is a Bitcoin Block in canonical form.
 * The FastBlock object is a thin wrapper around a buffer of data which is known to be a Bitcoin block.
 * The class provides access to all parts of the block as expected, but it has no parsing or loading state
 * that it has to go through before you can access it. As such deep-copying is always cheap.
 *
 * @see CBlock, Tx
 */
class FastBlock
{
public:
    /// Create invalid block
    FastBlock();
    /// Constructs a block from a buffer, notice that the buffer has to be at least 80 bytes as that is the block header size.
    FastBlock(const Streaming::ConstBuffer &rawBlock);
    FastBlock(const FastBlock &other) = default;

    /// returns the version of the block.
    int32_t blockVersion() const;
    /// Returns the hash pointer of the previous block in the chain.
    uint256 previousBlockId() const;
    /// returns the hash of the merkele root.
    uint256 merkleRoot() const;
    /// retuns the timestamp, as contained in the block header.
    uint32_t timestamp() const;
    /// retuns the bits field, as contained in the block header.
    uint32_t bits() const;
    /// retuns the nonce field, as contained in the block header.
    uint32_t nonce() const;

    /// calculates and returns hash.
    uint256 createHash() const;

    bool isFullBlock() const;

    /// Populate the transactions() array, throws exception on failure.
    /// This method should only ever be called once on a FastBlock.
    void findTransactions();

    /// Return transactions. @see findTransactions();
    inline const std::vector<Tx> &transactions() const {
        return m_transactions;
    }

    /// return the total size of this block.
    inline int size() const {
        return m_data.size();
    }

    /// For backwards compatibility with old code, load a CBlock and return it.
    CBlock createOldBlock() const;

    /**
     * @brief fromOldBlock saves the old block in a buffer which it returns a FastBlock instance with.
     * @param block the old block, which can be discarded afterwards.
     * @param pool an optional bufferPool, if not passed a local one will be created.
     * @return the newly created FastBlock
     */
    static FastBlock fromOldBlock(const CBlockHeader &block, Streaming::BufferPool *pool = 0);
    static FastBlock fromOldBlock(const CBlock &block, Streaming::BufferPool *pool = 0);

    /// \internal
    inline Streaming::ConstBuffer data() const {
        return m_data;
    }

private:
    Streaming::ConstBuffer m_data;
    std::vector<Tx> m_transactions;
};

#endif
