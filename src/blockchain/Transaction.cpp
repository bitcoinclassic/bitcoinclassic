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
#include "Transaction.h"
#include <primitives/transaction.h>

#include <compat/endian.h>

#include <hash.h>
#include <streams.h>

namespace {

}

Tx::Tx(const Streaming::ConstBuffer &rawTransaction)
    : m_data(rawTransaction)
{
}



uint32_t Tx::txVersion() const
{
    return static_cast<int32_t>(le32toh(*((uint32_t*)m_data.begin())));
}

uint256 Tx::createHash() const
{
    CHash256 ctx;
    ctx.Write((const unsigned char*) m_data.begin(), m_data.size());
    uint256 result;
    ctx.Finalize((unsigned char*)&result);
    return result;
}

CTransaction Tx::createOldTransaction() const
{
    CTransaction answer;
    CDataStream buf(m_data.begin(), m_data.end(), 0 , 0);
    answer.Unserialize(buf, 0, 0);
    return std::move(answer);
}
