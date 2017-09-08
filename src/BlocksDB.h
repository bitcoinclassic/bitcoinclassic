/*
 * This file is part of the bitcoin-classic project
 * Copyright (c) 2009-2010 Satoshi Nakamoto
 * Copyright (c) 2009-2015 The Bitcoin Core developers
 * Copyright (c) 2017 Tom Zander <tomz@freedommail.ch>
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

#ifndef BITCOIN_BLOCKSDB_H
#define BITCOIN_BLOCKSDB_H

#include "dbwrapper.h"

#include <blockchain/UndoBlock.h>
#include <boost/unordered_map.hpp>
#include <streaming/ConstBuffer.h>
#include <string>
#include <vector>

class CBlockFileInfo;
class CBlockIndex;
struct CDiskTxPos;
struct CDiskBlockPos;
class CChainParams;
class uint256;
class FastBlock;
class CChain;

//! -dbcache default (MiB)
static const int64_t nDefaultDbCache = 300;
//! max. -dbcache in (MiB)
static const int64_t nMaxDbCache = sizeof(void*) > 4 ? 16384 : 1024;
//! min. -dbcache in (MiB)
static const int64_t nMinDbCache = 4;

namespace Blocks {

class DBPrivate;

/** Access to the block database (blocks/index/) */
class DB : public CDBWrapper
{
public:
    /**
     * returns the singleton instance of the BlocksDB. Please be aware that
     *     this will return nullptr until you call createInstance() or createTestInstance();
     */
    static DB *instance();
    /**
     * Deletes an old and creates a new instance of the BlocksDB singleton.
     * @param[in] nCacheSize  Configures various leveldb cache settings.
     * @param[in] fWipe       If true, remove all existing data.
     * @see instance()
     */
    static void createInstance(size_t nCacheSize, bool fWipe);
    /// Deletes old singleton and creates a new one for unit testing.
    static void createTestInstance(size_t nCacheSize);

    /**
     * @brief starts the blockImporter part of a 'reindex'.
     * This kicks off a new thread that reads each file and schedules each block for
     * validation.
     */
    static void startBlockImporter();

    virtual ~DB();

protected:
    DB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);
    DB(const Blocks::DB&) = delete;
    void operator=(const DB&) = delete;
public:
    bool WriteBatchSync(const std::vector<std::pair<int, const CBlockFileInfo*> >& fileInfo, int nLastFile, const std::vector<const CBlockIndex*>& blockinfo);
    bool ReadBlockFileInfo(int nFile, CBlockFileInfo &fileinfo);
    bool ReadLastBlockFile(int &nFile);
    bool ReadTxIndex(const uint256 &txid, CDiskTxPos &pos);
    bool WriteTxIndex(const std::vector<std::pair<uint256, CDiskTxPos> > &list);
    bool WriteFlag(const std::string &name, bool fValue);
    bool ReadFlag(const std::string &name, bool &fValue);
    /// Reads and caches all info about blocks.
    bool CacheAllBlockInfos();

    bool isReindexing() const;
    bool setIsReindexing(bool fReindex);

    FastBlock loadBlock(CDiskBlockPos pos);
    FastUndoBlock loadUndoBlock(CDiskBlockPos pos, const uint256 &origBlockHash);
    Streaming::ConstBuffer loadBlockFile(int fileIndex);
    FastBlock writeBlock(int blockHeight, const FastBlock &block, CDiskBlockPos &pos);
    /**
     * @brief This method writes out the undo block to a specific file and belonging to a specific /a blockHash.
     * @param block The actual undo block
     * @param blockHash the hash of the parent block
     * @param fileIndex the index the original block was written to, this determines which revert index this block goes to.
     * @param posInFile a return value of the position this block ended up in.
     */
    FastUndoBlock writeUndoBlock(const FastUndoBlock &block, const uint256 &blockHash, int fileIndex, uint32_t *posInFile = 0);

    /**
     * @brief make the blocks-DB aware of a new header-only tip.
     * Add the parially validated block to the blocks database and import all parent
     * blocks at the same time.
     * This potentially updates the headerChain() and headerChainTips().
     * @param block the index to the block object.
     * @returns true if the header became the new main-chain tip.
     */
    bool appendHeader(CBlockIndex *block);
    /// allow adding one block, this API is primarily meant for unit tests.
    bool appendBlock(CBlockIndex *block, int lastBlockFile);

    const CChain &headerChain();
    const std::list<CBlockIndex*> & headerChainTips();

    void loadConfig();

    /// \internal
    DBPrivate *priv() {
        return d;
    }

private:
    static DB *s_instance;
    DBPrivate* d;
};

struct BlockHashShortener {
    inline size_t operator()(const uint256& hash) const {
        return hash.GetCheapHash();
    }
};

/** Open a block file (blk?????.dat) */
FILE* openFile(const CDiskBlockPos &pos, bool fReadOnly);
/** Open an undo file (rev?????.dat) */
FILE* openUndoFile(const CDiskBlockPos &pos, bool fReadOnly);
/**
 * Translation to a filesystem path.
 * @param fileIndex the number. For instance blk12345.dat is 12345.
 * @param prefix either "blk" or "rev"
 * @param fFindHarder set this to true if you want a path outside our main data-directory
 */
boost::filesystem::path getFilepathForIndex(int fileIndex, const char *prefix, bool fFindHarder = false);

// Protected by cs_main
typedef boost::unordered_map<uint256, CBlockIndex*, BlockHashShortener> BlockMap;
// TODO move this into BlocksDB and protect it with a mutex
extern BlockMap indexMap;
}



#endif // BITCOIN_TXDB_H
