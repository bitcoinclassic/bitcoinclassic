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

#ifndef BITCOIN_BLOCKCHAIN_UNDOBLOCK_H
#define BITCOIN_BLOCKCHAIN_UNDOBLOCK_H

#include <streaming/ConstBuffer.h>
#include <undo.h>

#include <vector>

class CBlockUndo;

namespace Streaming {
    class BufferPool;
}

class FastUndoBlock
{
public:
    /// Create invalid block
    FastUndoBlock();
    /// Constructs a block from a buffer, notice that the buffer has to be at least 80 bytes as that is the block header size.
    FastUndoBlock(const Streaming::ConstBuffer &rawBlock);
    FastUndoBlock(const FastUndoBlock &other) = default;

    /// return the total size of this block.
    inline int size() const {
        return m_data.size();
    }

    /// For backwards compatibility with old code, load a CBlock and return it.
    CBlockUndo createOldBlock() const;

    /**
     * @brief fromOldBlock saves the old block in a buffer which it returns a CFastUndoBlock instance with.
     * @param block the old block, which can be discarded afterwards.
     * @param pool an optional bufferPool, if not passed a local one will be created.
     * @return the newly created CFastUndoBlock
     */
    static FastUndoBlock fromOldBlock(const CBlockUndo &block, Streaming::BufferPool *pool = 0);

    /// \internal
    inline Streaming::ConstBuffer data() const {
        return m_data;
    }

private:
    Streaming::ConstBuffer m_data;
};

#endif
