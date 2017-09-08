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


#ifndef BITCOIN_BLOCKCHAIN_TRANSACTION_H
#define BITCOIN_BLOCKCHAIN_TRANSACTION_H

#include <streaming/ConstBuffer.h>

#include <uint256.h>

class CTransaction;

/**
 * @brief The Tx class is a Bitcoin transaction in canonical form.
 * The Tx object is a thin wrapper around a buffer of data which is known to be a Bitcoin transaction.
 *
 * @see CTransaction, FastBlock
 */
class Tx
{
public:
    /// creates invalid transaction.
    Tx();

    Tx(const Streaming::ConstBuffer &rawTransaction);
    // Tx(const Tx &other); // mark as having default implementation.
    // TODO assignment and copy constructor

    /**
     * @brief isValid returns true if it has a known backing memory store.
     * Notice that this method doesn't do validation of the transaction data.
     */
    inline bool isValid() const {
        return m_data.isValid();
    }

    /**
     * Returns the version number of a transaction.
     */
    uint32_t txVersion() const;

    /**
     * Hashes the transaction content and returns the sha256 double hash.
     * The hash is often also called the transaction-ID.
     */
    uint256 createHash() const;

    /**
     * for backwards compatibility with existing code this loads the transaction into a CTransaction class.
     */
    CTransaction createOldTransaction() const;

    /**
     * @return the bytecount of this transaction.
     */
    inline int size() const {
        return m_data.size();
    }

private:
    Streaming::ConstBuffer m_data;
};

#endif
