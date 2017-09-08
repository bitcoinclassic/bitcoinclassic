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
#include "UndoBlock.h"

#include <streams.h>
#include <undo.h>
#include <streaming/BufferPool.h>

FastUndoBlock::FastUndoBlock()
{
}

FastUndoBlock::FastUndoBlock(const Streaming::ConstBuffer &rawBlock)
    : m_data(rawBlock)
{
}

CBlockUndo FastUndoBlock::createOldBlock() const
{
    CBlockUndo answer;
    CDataStream buf(m_data.begin(), m_data.end(), 0 , 0);
    answer.Unserialize(buf, 0, 0);
    return std::move(answer);
}

FastUndoBlock FastUndoBlock::fromOldBlock(const CBlockUndo &block, Streaming::BufferPool *pool)
{
    CSizeComputer sc(0, 0);
    sc << block;
    if (pool) {
        pool->reserve(sc.size());
        block.Serialize(*pool, 0, 0);
        return FastUndoBlock(pool->commit());
    }
    Streaming::BufferPool pl(sc.size());
    block.Serialize(pl, 0, 0);
    return FastUndoBlock(pl.commit());
}
