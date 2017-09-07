/*
 * This file is part of the bitcoin-classic project
 * Copyright (c) 2009-2010 Satoshi Nakamoto
 * Copyright (c) 2017 Tom Zander <tomz@freedommail.ch>
 * Copyright (c) 2017 Calin Culianu <calin.culianu@gmail.com>
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

#include "BlocksDB.h"
#include "BlocksDB_p.h"
#include "chainparams.h"
#include "consensus/validation.h"
#include "Application.h"
#include "init.h" // for StartShutdown
#include "hash.h"

#include "chain.h"
#include "main.h"
#include "uint256.h"
#include <boost/thread.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <blockchain/Block.h>

static const char DB_BLOCK_FILES = 'f';
static const char DB_TXINDEX = 't';
static const char DB_BLOCK_INDEX = 'b';

static const char DB_FLAG = 'F';
static const char DB_REINDEX_FLAG = 'R';
static const char DB_LAST_BLOCK = 'l';
static const char DB_UAHF_FORK_BLOCK = 'U';

namespace {
CBlockIndex * InsertBlockIndex(uint256 hash)
{
    if (hash.IsNull())
        return NULL;

    // Return existing
    auto mi = Blocks::indexMap.find(hash);
    if (mi != Blocks::indexMap.end())
        return (*mi).second;

    // Create new
    CBlockIndex* pindexNew = new CBlockIndex();
    mi = Blocks::indexMap.insert(std::make_pair(hash, pindexNew)).first;
    pindexNew->phashBlock = &((*mi).first);

    return pindexNew;
}

bool LoadExternalBlockFile(const CChainParams& chainparams, FILE* fileIn, CDiskBlockPos *dbp = nullptr)
{
    // Map of disk positions for blocks with unknown parent (only used for reindex)
    static std::multimap<uint256, CDiskBlockPos> mapBlocksUnknownParent;
    int64_t nStart = GetTimeMillis();

    int nLoaded = 0;
    try {
        // This takes over fileIn and calls fclose() on it in the CBufferedFile destructor
        CBufferedFile blkdat(fileIn, 2E6, 1E6+8, SER_DISK, CLIENT_VERSION);
        uint64_t nRewind = blkdat.GetPos();
        while (!blkdat.eof()) {
            boost::this_thread::interruption_point();

            blkdat.SetPos(nRewind);
            nRewind++; // start one byte further next time, in case of failure
            blkdat.SetLimit(); // remove former limit
            unsigned int nSize = 0;
            try {
                // locate a header
                unsigned char buf[MESSAGE_START_SIZE];
                blkdat.FindByte(chainparams.MessageStart()[0]);
                nRewind = blkdat.GetPos()+1;
                blkdat >> FLATDATA(buf);
                if (memcmp(buf, chainparams.MessageStart(), MESSAGE_START_SIZE))
                    continue;
                // read size
                blkdat >> nSize;
                if (nSize < 80)
                    continue;
            } catch (const std::exception&) {
                // no valid block header found; don't complain
                break;
            }
            try {
                // read block
                uint64_t nBlockPos = blkdat.GetPos();
                if (dbp)
                    dbp->nPos = nBlockPos;
                blkdat.SetLimit(nBlockPos + nSize);
                blkdat.SetPos(nBlockPos);
                CBlock block;
                blkdat >> block;
                nRewind = blkdat.GetPos();

                // detect out of order blocks, and store them for later
                uint256 hash = block.GetHash();
                if (hash != chainparams.GetConsensus().hashGenesisBlock && Blocks::indexMap.find(block.hashPrevBlock) == Blocks::indexMap.end()) {
                    LogPrint("reindex", "%s: Out of order block %s, parent %s not known\n", __func__, hash.ToString(),
                            block.hashPrevBlock.ToString());
                    if (dbp)
                        mapBlocksUnknownParent.insert(std::make_pair(block.hashPrevBlock, *dbp));
                    continue;
                }

                // process in case the block isn't known yet
                if (Blocks::indexMap.count(hash) == 0 || (Blocks::indexMap[hash]->nStatus & BLOCK_HAVE_DATA) == 0) {
                    CValidationState state;
                    if (ProcessNewBlock(state, chainparams, NULL, &block, true, dbp))
                        nLoaded++;
                    if (state.IsError())
                        break;
                } else if (hash != chainparams.GetConsensus().hashGenesisBlock && Blocks::indexMap[hash]->nHeight % 1000 == 0) {
                    LogPrintf("Block Import: already had block %s at height %d\n", hash.ToString(), Blocks::indexMap[hash]->nHeight);
                }

                // Recursively process earlier encountered successors of this block
                std::deque<uint256> queue;
                queue.push_back(hash);
                while (!queue.empty()) {
                    uint256 head = queue.front();
                    queue.pop_front();
                    std::pair<std::multimap<uint256, CDiskBlockPos>::iterator, std::multimap<uint256, CDiskBlockPos>::iterator> range = mapBlocksUnknownParent.equal_range(head);
                    while (range.first != range.second) {
                        std::multimap<uint256, CDiskBlockPos>::iterator it = range.first;
                        if (ReadBlockFromDisk(block, it->second, chainparams.GetConsensus()))
                        {
                            LogPrintf("%s: Processing out of order child %s of %s\n", __func__, block.GetHash().ToString(),
                                    head.ToString());
                            CValidationState dummy;
                            if (ProcessNewBlock(dummy, chainparams, NULL, &block, true, &it->second))
                            {
                                nLoaded++;
                                queue.push_back(block.GetHash());
                            }
                        }
                        range.first++;
                        mapBlocksUnknownParent.erase(it);
                    }
                }
            } catch (const std::exception& e) {
                LogPrintf("%s: Deserialize or I/O error - %s\n", __func__, e.what());
            }
        }
    } catch (const std::runtime_error& e) {
        LogPrintf("%s: Deserialize or I/O error - %s\n", __func__, e.what());
        return false;
    }
    if (nLoaded > 0)
        LogPrintf("Loaded %i blocks from external file in %dms\n", nLoaded, GetTimeMillis() - nStart);
    return nLoaded > 0;
}

struct CImportingNow
{
    CImportingNow() {
        assert(fImporting == false);
        fImporting = true;
    }

    ~CImportingNow() {
        assert(fImporting == true);
        fImporting = false;
    }
};

void reimportBlockFiles(std::vector<boost::filesystem::path> vImportFiles)
{
    const CChainParams& chainparams = Params();
    RenameThread("bitcoin-loadblk");
    bool fReindex = Blocks::DB::instance()->isReindexing();

    if (fReindex) {
        CImportingNow imp;
        int nFile = 0;
        while (!ShutdownRequested()) {
            CDiskBlockPos pos(nFile, 0);
            if (!boost::filesystem::exists(Blocks::getFilepathForIndex(pos.nFile, "blk", true)))
                break; // No block files left to reindex
            FILE *file = Blocks::openFile(pos, true);
            if (!file)
                break; // This error is logged in OpenBlockFile
            LogPrintf("Reindexing block file blk%05u.dat...\n", (unsigned int)nFile);
            LoadExternalBlockFile(chainparams, file, &pos);
            nFile++;
        }
        Blocks::DB::instance()->setIsReindexing(false);
        fReindex = false;
        LogPrintf("Reindexing finished\n");
        // To avoid ending up in a situation without genesis block, re-try initializing (no-op if reindexing worked):
        InitBlockIndex(chainparams);
    }

    // hardcoded $DATADIR/bootstrap.dat
    boost::filesystem::path pathBootstrap = GetDataDir() / "bootstrap.dat";
    if (boost::filesystem::exists(pathBootstrap)) {
        FILE *file = fopen(pathBootstrap.string().c_str(), "rb");
        if (file) {
            CImportingNow imp;
            boost::filesystem::path pathBootstrapOld = GetDataDir() / "bootstrap.dat.old";
            LogPrintf("Importing bootstrap.dat...\n");
            LoadExternalBlockFile(chainparams, file);
            RenameOver(pathBootstrap, pathBootstrapOld);
        } else {
            LogPrintf("Warning: Could not open bootstrap file %s\n", pathBootstrap.string());
        }
    }

    // -loadblock=
    BOOST_FOREACH(const boost::filesystem::path& path, vImportFiles) {
        FILE *file = fopen(path.string().c_str(), "rb");
        if (file) {
            CImportingNow imp;
            LogPrintf("Importing blocks file %s...\n", path.string());
            LoadExternalBlockFile(chainparams, file);
        } else {
            LogPrintf("Warning: Could not open blocks file %s\n", path.string());
        }
    }

    if (GetBoolArg("-stopafterblockimport", DEFAULT_STOPAFTERBLOCKIMPORT)) {
        LogPrintf("Stopping after block import\n");
        StartShutdown();
    }
}

}

namespace Blocks {
BlockMap indexMap;
}


Blocks::DB* Blocks::DB::s_instance = nullptr;

Blocks::DB *Blocks::DB::instance()
{
    return Blocks::DB::s_instance;
}

void Blocks::DB::createInstance(size_t nCacheSize, bool fWipe)
{
    Blocks::indexMap.clear();
    delete Blocks::DB::s_instance;
    Blocks::DB::s_instance = new Blocks::DB(nCacheSize, false, fWipe);
}

void Blocks::DB::createTestInstance(size_t nCacheSize)
{
    Blocks::indexMap.clear();
    delete Blocks::DB::s_instance;
    Blocks::DB::s_instance = new Blocks::DB(nCacheSize, true);
}

void Blocks::DB::startBlockImporter()
{
    std::vector<boost::filesystem::path> vImportFiles;
    if (mapArgs.count("-loadblock"))
    {
        BOOST_FOREACH(const std::string& strFile, mapMultiArgs["-loadblock"])
            vImportFiles.push_back(strFile);
    }
    Application::createThread(std::bind(&reimportBlockFiles, vImportFiles));
}



Blocks::DB::DB(size_t nCacheSize, bool fMemory, bool fWipe)
    : CDBWrapper(GetDataDir() / "blocks" / "index", nCacheSize, fMemory, fWipe),
      d(new DBPrivate())
{
    d->isReindexing = Exists(DB_REINDEX_FLAG);
    loadConfig();
}

Blocks::DB::~DB()
{
    delete d;
}

bool Blocks::DB::ReadBlockFileInfo(int nFile, CBlockFileInfo &info) {
    return Read(std::make_pair(DB_BLOCK_FILES, nFile), info);
}

bool Blocks::DB::setIsReindexing(bool fReindexing) {
    if (d->isReindexing == fReindexing)
        return true;
    d->isReindexing = fReindexing;
    if (fReindexing)
        return Write(DB_REINDEX_FLAG, '1');
    else
        return Erase(DB_REINDEX_FLAG);
}

bool Blocks::DB::ReadLastBlockFile(int &nFile) {
    return Read(DB_LAST_BLOCK, nFile);
}

bool Blocks::DB::WriteBatchSync(const std::vector<std::pair<int, const CBlockFileInfo*> >& fileInfo, int nLastFile, const std::vector<const CBlockIndex*>& blockinfo) {
    CDBBatch batch(&GetObfuscateKey());
    for (std::vector<std::pair<int, const CBlockFileInfo*> >::const_iterator it=fileInfo.begin(); it != fileInfo.end(); it++) {
        batch.Write(std::make_pair(DB_BLOCK_FILES, it->first), *it->second);
    }
    batch.Write(DB_LAST_BLOCK, nLastFile);
    for (std::vector<const CBlockIndex*>::const_iterator it=blockinfo.begin(); it != blockinfo.end(); it++) {
        batch.Write(std::make_pair(DB_BLOCK_INDEX, (*it)->GetBlockHash()), CDiskBlockIndex(*it));
    }
    return WriteBatch(batch, true);
}

bool Blocks::DB::ReadTxIndex(const uint256 &txid, CDiskTxPos &pos) {
    return Read(std::make_pair(DB_TXINDEX, txid), pos);
}

bool Blocks::DB::WriteTxIndex(const std::vector<std::pair<uint256, CDiskTxPos> >&vect) {
    CDBBatch batch(&GetObfuscateKey());
    for (std::vector<std::pair<uint256,CDiskTxPos> >::const_iterator it=vect.begin(); it!=vect.end(); it++)
        batch.Write(std::make_pair(DB_TXINDEX, it->first), it->second);
    return WriteBatch(batch);
}

bool Blocks::DB::WriteFlag(const std::string &name, bool fValue) {
    return Write(std::make_pair(DB_FLAG, name), fValue ? '1' : '0');
}

bool Blocks::DB::ReadFlag(const std::string &name, bool &fValue) {
    char ch;
    if (!Read(std::make_pair(DB_FLAG, name), ch))
        return false;
    fValue = ch == '1';
    return true;
}

bool Blocks::DB::CacheAllBlockInfos()
{
    boost::scoped_ptr<CDBIterator> pcursor(NewIterator());

    pcursor->Seek(std::make_pair(DB_BLOCK_INDEX, uint256()));
    int maxFile = 0;

    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        std::pair<char, uint256> key;
        if (pcursor->GetKey(key) && key.first == DB_BLOCK_INDEX) {
            CDiskBlockIndex diskindex;
            if (pcursor->GetValue(diskindex)) {
                // Construct block index object
                CBlockIndex* pindexNew = InsertBlockIndex(diskindex.GetBlockHash());
                pindexNew->pprev          = InsertBlockIndex(diskindex.hashPrev);
                pindexNew->nHeight        = diskindex.nHeight;
                pindexNew->nFile          = diskindex.nFile;
                maxFile = std::max(pindexNew->nFile, maxFile);
                pindexNew->nDataPos       = diskindex.nDataPos;
                pindexNew->nUndoPos       = diskindex.nUndoPos;
                pindexNew->nVersion       = diskindex.nVersion;
                pindexNew->hashMerkleRoot = diskindex.hashMerkleRoot;
                pindexNew->nTime          = diskindex.nTime;
                pindexNew->nBits          = diskindex.nBits;
                pindexNew->nNonce         = diskindex.nNonce;
                pindexNew->nStatus        = diskindex.nStatus;
                pindexNew->nTx            = diskindex.nTx;

                pcursor->Next();
            } else {
                return error("CacheAllBlockInfos(): failed to read row");
            }
        } else {
            break;
        }
    }
    d->datafiles.resize(maxFile);
    d->revertDatafiles.resize(maxFile);

    for (auto iter = Blocks::indexMap.begin(); iter != Blocks::indexMap.end(); ++iter) {
        iter->second->BuildSkip();
    }
//   according to reports (github issue 276) this is too slow for some reason. Lets
//   turn this off for now.
//   for (auto iter = Blocks::indexMap.begin(); iter != Blocks::indexMap.end(); ++iter) {
//       appendHeader(iter->second);
//   }

    return true;
}

bool Blocks::DB::isReindexing() const
{
    return d->isReindexing;
}

static FILE* OpenDiskFile(const CDiskBlockPos &pos, const char *prefix, bool fReadOnly)
{
    if (pos.IsNull())
        return NULL;
    boost::filesystem::path path = Blocks::getFilepathForIndex(pos.nFile, prefix);
    boost::filesystem::create_directories(path.parent_path());
    FILE* file = fopen(path.string().c_str(), "rb+");
    if (!file && !fReadOnly)
        file = fopen(path.string().c_str(), "wb+");
    if (!file) {
        LogPrintf("Unable to open file %s\n", path.string());
        return NULL;
    }
    if (pos.nPos) {
        if (fseek(file, pos.nPos, SEEK_SET)) {
            LogPrintf("Unable to seek to position %u of %s\n", pos.nPos, path.string());
            fclose(file);
            return NULL;
        }
    }
    return file;
}

FILE* Blocks::openFile(const CDiskBlockPos &pos, bool fReadOnly) {
    return OpenDiskFile(pos, "blk", fReadOnly);
}

FILE* Blocks::openUndoFile(const CDiskBlockPos &pos, bool fReadOnly) {
    return OpenDiskFile(pos, "rev", fReadOnly);
}

boost::filesystem::path Blocks::getFilepathForIndex(int fileIndex, const char *prefix, bool fFindHarder)
{
    auto path = GetDataDir() / "blocks" / strprintf("%s%05u.dat", prefix, fileIndex);
    if (fFindHarder && !boost::filesystem::exists(path)) {
        DBPrivate *d = Blocks::DB::instance()->priv();
        for (const std::string &dir : d->blocksDataDirs) {
            boost::filesystem::path alternatePath(dir);
            alternatePath = alternatePath / "blocks" / strprintf("%s%05u.dat", prefix, fileIndex);
            if (boost::filesystem::exists(alternatePath))
                return alternatePath;
        }
    }
    return path;
}

FastBlock Blocks::DB::loadBlock(CDiskBlockPos pos)
{
    return FastBlock(d->loadBlock(pos, ForwardBlock, 0));
}

FastUndoBlock Blocks::DB::loadUndoBlock(CDiskBlockPos pos, const uint256 &origBlockHash)
{
    return FastUndoBlock(d->loadBlock(pos, RevertBlock, &origBlockHash));
}

Streaming::ConstBuffer Blocks::DB::loadBlockFile(int fileIndex)
{
    try {
        size_t fileSize;
        auto buf = d->mapFile(fileIndex, ForwardBlock, &fileSize);
        if (buf.get() == nullptr)
            return Streaming::ConstBuffer(); // got pruned
        return Streaming::ConstBuffer(buf, buf.get(), buf.get() + fileSize - 1);
    } catch (const std::ios_base::failure &ex) {
        return Streaming::ConstBuffer(); // file missing.
    }
}

FastBlock Blocks::DB::writeBlock(int blockHeight, const FastBlock &block, CDiskBlockPos &pos)
{
    assert(block.isFullBlock());
    return FastBlock(d->writeBlock(blockHeight, block.data(), pos, ForwardBlock, block.timestamp(), 0));
}

FastUndoBlock Blocks::DB::writeUndoBlock(const FastUndoBlock &block, const uint256 &blockHash, int fileIndex, uint32_t *posInFile)
{
    assert(block.size() > 0);
    CDiskBlockPos pos(fileIndex, 0);
    FastUndoBlock answer(d->writeBlock(0, block.data(), pos, RevertBlock, 0, &blockHash));
    if (posInFile)
        *posInFile = pos.nPos;
    return answer;
}

bool Blocks::DB::appendHeader(CBlockIndex *block)
{
    assert(block);
    assert(block->phashBlock);
    bool found = false;
    const bool valid = (block->nStatus & BLOCK_FAILED_MASK) == 0;
    assert(valid || block->pprev);  // can't mark the genesis as invalid.
    for (auto i = d->headerChainTips.begin(); i != d->headerChainTips.end(); ++i) {
        CBlockIndex *tip = *i;
        CBlockIndex *parent = block;
        while (parent && parent->nHeight > tip->nHeight) {
            parent = parent->pprev;
        }
        if (parent == tip) {
            if (!valid)
                block = block->pprev;
            d->headerChainTips.erase(i);
            d->headerChainTips.push_back(block);
            if (tip == d->headersChain.Tip()) {
                d->headersChain.SetTip(block);
                pindexBestHeader = block;
                return true;
            }
            found = true;
            break;
        }
    }

    if (!found) {
        for (auto i = d->headerChainTips.begin(); i != d->headerChainTips.end(); ++i) {
            if ((*i)->GetAncestor(block->nHeight) == block) { // known in this chain.
                if (valid)
                    return false;
                // if it is invalid, remove it and all children.
                const bool modifyingMainChain = d->headersChain.Contains(*i);
                d->headerChainTips.erase(i);
                block = block->pprev;
                d->headerChainTips.push_back(block);
                if (modifyingMainChain)
                    d->headersChain.SetTip(block);
                return modifyingMainChain;
            }
        }
        if (valid) {
            d->headerChainTips.push_back(block);
            if (d->headersChain.Height() == -1) { // add genesis
                d->headersChain.SetTip(block);
                pindexBestHeader = block;
                return true;
            }
        }
    }
    if (d->headersChain.Tip()->nChainWork < block->nChainWork) {
        // we changed what is to be considered the main-chain. Update the CChain instance.
        d->headersChain.SetTip(block);
        pindexBestHeader = block;
        return true;
    }
    return false;
}

bool Blocks::DB::appendBlock(CBlockIndex *block, int lastBlockFile)
{
    std::vector<std::pair<int, const CBlockFileInfo*> > files;
    std::vector<const CBlockIndex*> blocks;
    blocks.push_back(block);
    return WriteBatchSync(files, lastBlockFile, blocks);
}

const CChain &Blocks::DB::headerChain()
{
    return d->headersChain;
}

const std::list<CBlockIndex *> &Blocks::DB::headerChainTips()
{
    return d->headerChainTips;
}

void Blocks::DB::loadConfig()
{
    d->blocksDataDirs.clear();

    for (auto dir : mapMultiArgs["-blockdatadir"]) {
        if (boost::filesystem::is_directory(boost::filesystem::path(dir) / "blocks")) {
            d->blocksDataDirs.push_back(dir);
        } else {
            logCritical(4000) << "invalid blockdatadir passed. No 'blocks' subdir found, skipping:"<< dir;
        }
    }
}


///////////////////////////////////////////////

Blocks::DBPrivate::DBPrivate()
    : isReindexing(false),
      uahfStartBlock(nullptr)
{
}

Blocks::DBPrivate::~DBPrivate()
{
    for (auto file : datafiles) {
        delete file;
    }
    for (auto file : revertDatafiles) {
        delete file;
    }
}

Streaming::ConstBuffer Blocks::DBPrivate::loadBlock(CDiskBlockPos pos, BlockType type, const uint256 *blockHash)
{
    if (pos.nPos < 4)
        throw std::runtime_error("Blocks::loadBlock got Database corruption");
    size_t fileSize;
    auto buf = mapFile(pos.nFile, type, &fileSize);
    if (buf.get() == nullptr)
        throw std::runtime_error("Failed to memmap block");
    if (pos.nPos >= fileSize)
        throw std::runtime_error("position outside of file");
    uint32_t blockSize = le32toh(*((uint32_t*)(buf.get() + pos.nPos - 4)));
    if (pos.nPos + blockSize + (blockHash ? 32 : 0) > fileSize)
        throw std::runtime_error("block sized bigger than file");
    if (blockHash) {
        assert(type == RevertBlock);
        // Verify checksum
        CHashWriter hasher(SER_GETHASH, PROTOCOL_VERSION);
        hasher << *blockHash;
        hasher.write(buf.get() + pos.nPos, blockSize);
        uint256 hashChecksum(buf.get() + pos.nPos + blockSize);

        if (hashChecksum != hasher.GetHash())
            throw std::runtime_error("BlocksDB::loadUndoBlock, checkum mismatch");
    }
    return Streaming::ConstBuffer(buf, buf.get() + pos.nPos, buf.get() + pos.nPos + blockSize);
}

Streaming::ConstBuffer Blocks::DBPrivate::writeBlock(int blockHeight, const Streaming::ConstBuffer &block, CDiskBlockPos &pos, BlockType type, uint32_t timestamp, const uint256 *blockHash)
{
    const int blockSize = block.size();
    assert(blockSize < (int) MAX_BLOCKFILE_SIZE - 8);
    LOCK(cs_LastBlockFile);

    bool newFile = false;
    const bool useBlk = type == ForwardBlock;
    if ((int) vinfoBlockFile.size() <= nLastBlockFile) { // first file.
        newFile = true;
        vinfoBlockFile.resize(nLastBlockFile + 1);
    } else if (useBlk && vinfoBlockFile[nLastBlockFile].nSize + blockSize + 8 > MAX_BLOCKFILE_SIZE) {
        // previous file full.
        newFile = true;
        vinfoBlockFile.resize(++nLastBlockFile + 1);
    }
    if (useBlk) // revert files get to tell us which file they want to be in
        pos.nFile = nLastBlockFile;
    CBlockFileInfo &info = vinfoBlockFile[pos.nFile];
    if (newFile || (!useBlk && info.nUndoSize == 0)) {
        const auto path = getFilepathForIndex(pos.nFile, useBlk ? "blk" : "rev");
        logDebug(Log::DB) << "Starting new file" << path.string();
        std::lock_guard<std::mutex> lock_(lock);
        size_t newFileSize = std::max(blockSize, (int) (useBlk ? BLOCKFILE_CHUNK_SIZE : UNDOFILE_CHUNK_SIZE));
#ifdef WIN32
        // due to the fact that on Windows we can't re-map, we skip the growing steps.
        newFileSize = MAX_BLOCKFILE_SIZE;
#endif
        boost::filesystem::ofstream file(path);
        file.close();
        boost::filesystem::resize_file(path, newFileSize);
    }
    size_t fileSize;
    auto buf = mapFile(pos.nFile, type, &fileSize);
    if (buf.get() == nullptr)
        throw std::runtime_error("Failed to open file");
    uint32_t *posInFile = useBlk ? &info.nSize : &info.nUndoSize;
#ifndef WIN32
    if (*posInFile + blockSize + 8 > fileSize) {
        const auto path = getFilepathForIndex(pos.nFile, useBlk ? "blk" : "rev");
        logDebug(Log::DB) << "File" << path.string() << "needs to be resized";
        const size_t newFileSize = fileSize + (useBlk ? BLOCKFILE_CHUNK_SIZE : UNDOFILE_CHUNK_SIZE);
        { // scope the lock
            std::lock_guard<std::mutex> lock_(lock);
            boost::filesystem::resize_file(path, newFileSize);
            useBlk ? fileHasGrown(pos.nFile) : revertFileHasGrown(pos.nFile);
        }
        buf = mapFile(pos.nFile, type, &fileSize);
    }
#endif
    pos.nPos = *posInFile + 8;
    char *data = buf.get() + *posInFile;
    memcpy(data, Params().MessageStart(), 4);
    data += 4;
    uint32_t networkSize = htole32(blockSize);
    memcpy(data, &networkSize, 4);
    data += 4;
    memcpy(data, block.begin(), blockSize);
    if (type == ForwardBlock) {
        info.AddBlock(blockHeight, timestamp);
    } else {
        assert(type == RevertBlock);
        assert(blockHash);
        // calculate & write checksum
        CHashWriter hasher(SER_GETHASH, PROTOCOL_VERSION);
        hasher << *blockHash;
        hasher.write(block.begin(), block.size());
        uint256 hash = hasher.GetHash();
        memcpy(data + blockSize, hash.begin(), 256 / 8);
        *posInFile += 32;
    }
    *posInFile += blockSize + 8;
    setDirtyFileInfo.insert(pos.nFile);
    return Streaming::ConstBuffer(buf, data, data + blockSize);
}

std::shared_ptr<char> Blocks::DBPrivate::mapFile(int fileIndex, Blocks::BlockType type, size_t *size_out)
{
    const bool useBlk = type == ForwardBlock;
    std::vector<DataFile*> &list = useBlk ? datafiles : revertDatafiles;
    const char *prefix = useBlk ? "blk" : "rev";

    std::lock_guard<std::mutex> lock_(lock);
    if ((int) list.size() <= fileIndex)
        list.resize(fileIndex + 10);
    DataFile *df = list.at(fileIndex);
    if (df == nullptr) {
        df = new DataFile();
        list[fileIndex] = df;
    }
    std::shared_ptr<char> buf = df->buffer.lock();
    if (buf.get() == nullptr) {
        auto path = getFilepathForIndex(fileIndex, prefix);
        auto mode = std::ios_base::binary | std::ios_base::in;
        if (fileIndex == nLastBlockFile) // limit writable bit only to the last file.
            mode |= std::ios_base::out;
        df->file.open(path, mode);
        if (df->file.is_open()) {
            auto cleanupLambda = [useBlk,fileIndex,df,this] (char *buf) {
                {   // mutex scope...
                    std::lock_guard<std::mutex> lockG(lock);
                    std::vector<DataFile*> &list = useBlk ? datafiles : revertDatafiles;
                    assert(fileIndex >= 0 && fileIndex < (int) list.size());
                    if (df == list[fileIndex])
                        // invalidate entry -- note that it's possible
                        // df != list[fileIndex] if we resized the file
                        list[fileIndex] = nullptr;
                }
                // no need to hold lock on delete -- auto-closes mmap'd file.
                delete df;
            };
            buf = std::shared_ptr<char>(const_cast<char*>(df->file.const_data()), cleanupLambda);
            df->buffer = std::weak_ptr<char>(buf);
            df->filesize = df->file.size();
        } else {
            logCritical(Log::DB) << "Blocks::DB: failed to memmap data-file" << path.string();
        }
    }
    if (size_out) *size_out = df->filesize;
    return buf;
}

// we expect the mutex `lock` to be locked before calling this method
void Blocks::DBPrivate::fileHasGrown(int fileIndex)
{
    if (fileIndex < 0 || fileIndex >= int(datafiles.size()))
        // silently ignore invalid usage as it creates no harm
        return;
    // unconditionally invalidate the pointer.
    // This doesn't leak memory because if ptr existed, there are
    // extant shard_ptr buffers.  When they get deleted, ptr will also.
    // (see cleanupLambda in mapFile() above)
    datafiles[fileIndex] = nullptr;
}

// we expect the mutex `lock` to be locked before calling this method
void Blocks::DBPrivate::revertFileHasGrown(int fileIndex)
{
    if (fileIndex < 0 || fileIndex >= int(revertDatafiles.size()))
        // silently ignore invalid usage as it creates no harm
        return;
    // unconditionally invalidate the pointer.
    // This doesn't leak memory because if ptr existed, there are
    // extant shard_ptr buffers.  When they get deleted, ptr will also.
    // (see cleanupLambda in mapFile() above)
    revertDatafiles[fileIndex] = nullptr;
}

///////////////////////////////////////////////
