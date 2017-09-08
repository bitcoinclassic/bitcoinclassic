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

#ifndef BITCOIN_BLOCKCHAIN_BLOCK_P_H
#define BITCOIN_BLOCKCHAIN_BLOCK_P_H

#include "chain.h"
#include "BlocksDB.h"
#include "streaming/ConstBuffer.h"

#include <vector>
#include <mutex>
#include <memory>
#include <list>

#include <boost/iostreams/device/mapped_file.hpp>

class CBlockIndex;

namespace Blocks {

struct DataFile {
    DataFile() : filesize(0) {}
    boost::iostreams::mapped_file file;
    std::weak_ptr<char> buffer;
    int filesize;
};

enum BlockType {
    ForwardBlock,
    RevertBlock
};

class DBPrivate {
public:
    DBPrivate();
    ~DBPrivate();

    bool isReindexing;

    Streaming::ConstBuffer loadBlock(CDiskBlockPos pos, BlockType type, const uint256 *blockHash);
    Streaming::ConstBuffer writeBlock(int blockHeight, const Streaming::ConstBuffer &block, CDiskBlockPos &pos, BlockType type, uint32_t timestamp, const uint256 *blockHash);

    std::shared_ptr<char> mapFile(int fileIndex, BlockType type, size_t *size_out = 0);

    // Notify this class that the block file in question has been extended.  Calling this method
    // is required whenever block files get written-to and their size changes.  If this method
    // isn't called, mapFile() will continue to return memory from the old block file size until
    // all extant shared_ptr<char> bufs die.  Calling this method ensures that subsequent calls to
    // mapFile() will encompass the entire file.
    void fileHasGrown(int fileIndex);
    void revertFileHasGrown(int fileIndex);

    CChain headersChain;
    std::list<CBlockIndex*> headerChainTips;
    CBlockIndex *uahfStartBlock;

    std::vector<std::string> blocksDataDirs;

    std::mutex lock;
    std::vector<DataFile*> datafiles;
    std::vector<DataFile*> revertDatafiles;
};
}

#endif
