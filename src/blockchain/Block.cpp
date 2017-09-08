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
#include "Block.h"

#include <cassert>
#include <hash.h>
#include <streams.h>

#include <primitives/block.h>

#include <streaming/BufferPool.h>

namespace
{
uint64_t readCompactSize(const Streaming::ConstBuffer &buf, size_t &posInStream) {
    if ((size_t) buf.size() <= posInStream)
        throw std::runtime_error("readCompactSize not enough bytes");
    unsigned char byte = static_cast<unsigned char>(buf[posInStream]);
    if (byte < 253) {
        ++posInStream;
        return byte;
    }
    if (byte == 253) {// next 2 bytes
        if ((size_t) buf.size() <= posInStream + 3)
            throw std::runtime_error("readCompactSize not enough bytes");
        posInStream += 3;
        return le16toh(*((uint16_t*)(buf.begin() + posInStream - 2)));
    }
    if (byte == 254) {// next 4 bytes
        if ((size_t) buf.size() <= posInStream + 5)
            throw std::runtime_error("readCompactSize not enough bytes");
        posInStream += 5;
        return le32toh(*((uint32_t*)(buf.begin() + posInStream - 4)));
    }
    // next 8 bytes
    if ((size_t) buf.size() <= posInStream + 9)
        throw std::runtime_error("readCompactSize not enough bytes");
    posInStream += 9;
    return le64toh(*((uint64_t*)(buf.begin() + posInStream - 8)));
}

// from data, tell me how large a transaction is.
int transactionSize(const Streaming::ConstBuffer &buf, const int pos) {
    size_t positionInStream = pos + 4;
    uint64_t inCount = readCompactSize(buf, positionInStream);
    for (uint64_t i = 0; i < inCount; ++i) {
        positionInStream += 32 + 4;
        uint64_t scriptLength = readCompactSize(buf, positionInStream);
        positionInStream += scriptLength + 4;
        if (positionInStream > (size_t) buf.size() + pos)
            throw std::runtime_error("transaction malformed error");
    }
    uint64_t outCount = readCompactSize(buf, positionInStream);
    for (uint64_t i = 0; i < outCount; ++i) {
        positionInStream += 8;
        uint64_t scriptLength = readCompactSize(buf, positionInStream);
        positionInStream += scriptLength;
        if (positionInStream > (size_t) buf.size() + pos)
            throw std::runtime_error("transaction malformed error");
    }
    positionInStream += 4;
    return positionInStream - pos;
}
}

FastBlock::FastBlock()
{
}

FastBlock::FastBlock(const Streaming::ConstBuffer &rawBlock)
    : m_data(rawBlock)
{
    if (rawBlock.size() < 80)
        throw std::runtime_error("Block too small to fit header");
}

int32_t FastBlock::blockVersion() const
{
    return static_cast<int32_t>(le32toh(*((uint32_t*)m_data.begin())));
}

uint256 FastBlock::previousBlockId() const
{
    return uint256(m_data.begin() + 4);
}

uint256 FastBlock::merkleRoot() const
{
    return uint256(m_data.begin() + 36);
}

uint32_t FastBlock::timestamp() const
{
    return le32toh(*((uint32_t*)(m_data.begin() + 68)));
}

uint32_t FastBlock::bits() const
{
    return le32toh(*((uint32_t*)(m_data.begin() + 72)));
}

uint32_t FastBlock::nonce() const
{
    return le32toh(*((uint32_t*)(m_data.begin() + 76)));
}

uint256 FastBlock::createHash() const
{
    assert(m_data.size() >= 80);
    CHash256 ctx;
    ctx.Write((const unsigned char*)m_data.begin(), 80);
    uint256 result;
    ctx.Finalize((unsigned char*)&result);
    return result;
}

bool FastBlock::isFullBlock() const
{
    return m_data.size() > 80;
}

void FastBlock::findTransactions()
{
    if (!m_transactions.empty())
        return;
    size_t pos = 80;
    const int transactionCount = readCompactSize(m_data, pos);
    std::vector<Tx> txs;
    txs.reserve(transactionCount);
    for (int i = 0; i < transactionCount; ++i) {
        int txSize = transactionSize(m_data, pos);
        if (txSize + pos > (uint64_t) m_data.size())
            throw std::runtime_error("FastBlock::findTransactions: not enough bytes");
        txs.push_back(Tx(m_data.mid(pos, txSize)));
        pos += txSize;
    }
    m_transactions = std::move(txs);
}

CBlock FastBlock::createOldBlock() const
{
    if (!isFullBlock())
        throw std::runtime_error("Not enough bytes to create a block");
    CBlock answer;
    CDataStream buf(m_data.begin(), m_data.end(), 0 , 0);
    answer.Unserialize(buf, 0, 0);
    return std::move(answer);
}

FastBlock FastBlock::fromOldBlock(const CBlock &block, Streaming::BufferPool *pool)
{
    CSizeComputer sc(0, 0);
    sc << block;
    if (pool) {
        pool->reserve(sc.size());
        block.Serialize(*pool, 0, 0);
        return FastBlock(pool->commit());
    }
    Streaming::BufferPool pl(sc.size());
    block.Serialize(pl, 0, 0);
    return FastBlock(pl.commit());
}

FastBlock FastBlock::fromOldBlock(const CBlockHeader &block, Streaming::BufferPool *pool)
{
    CSizeComputer sc(0, 0);
    sc << block;
    if (pool) {
        pool->reserve(sc.size());
        block.Serialize(*pool, 0, 0);
        return FastBlock(pool->commit());
    }
    Streaming::BufferPool pl(sc.size());
    block.Serialize(pl, 0, 0);
    return FastBlock(pl.commit());
}
