/*
 * This file is part of the bitcoin-classic project
 * Copyright (C) 2017 Calin Culianu <calin.culianu@gmail.com>
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

#include "test/test_bitcoin.h"

#include <BlocksDB.h>
#include <BlocksDB_p.h>
#include <main.h>
#include <undo.h>
#include <util.h>
#include <streaming/BufferPool.h>
#include <blockchain/Block.h>
#include <blockchain/UndoBlock.h>

#include <boost/test/unit_test.hpp>


BOOST_FIXTURE_TEST_SUITE(blocksdb_mapfile_tests, TestingSetup)

#ifndef WIN32
static void writeToFiles(int nFiles, size_t n2w, size_t atPos) {
    for (int i = 0; i < nFiles; ++i) {
        CDiskBlockPos pos(i,atPos);
        FILE *f = Blocks::openFile(pos, false);
        size_t nw = 0;
        char buf[1024];
        memset(buf,char(i%128),sizeof(buf));
        while (nw < n2w) {
            const size_t bytes = std::min(n2w-nw,sizeof(buf));
            fwrite(buf, bytes, 1, f);
            nw += sizeof(buf);
        }
        fclose(f);
    }
}

BOOST_AUTO_TEST_CASE(mapFile_extendFile_test)
{
    Blocks::DBPrivate pvt, pvt2;

    // first create dummy block files
    const int nFiles = 100;
    const double szMB = 0.128;
    const size_t szBytes = szMB*1024*1024;
    LogPrintf("Creating %d dummy blk files, each %fMB in size...\n", nFiles, szMB);

    writeToFiles(nFiles, szBytes, 0);

    std::vector<std::pair<std::shared_ptr<char>, size_t> > bufs;
    bufs.reserve(nFiles);

    for (int i = 0; i < nFiles; ++i) {
        size_t filesz, filesz2;
        auto buf = pvt.mapFile(i, Blocks::ForwardBlock, &filesz);
        //LogPrintf("%d got size %d\n", i, int(filesz));
        auto buf2 = pvt.mapFile(i, Blocks::ForwardBlock, &filesz2);
        //LogPrintf("%d (second) got size %d\n", i, int(filesz2));
        BOOST_CHECK(filesz == filesz2);
        BOOST_CHECK(buf.get() != nullptr && buf.get()[filesz-1] == char(i%128));

        // keep buffers around to prevent mmap's from going away, heh ;)
        bufs.emplace_back(std::make_pair(buf, filesz));
    }

    LogPrintf("Extending %d dummy blk files by %fMB each...\n", nFiles, szMB);
    writeToFiles(nFiles, szBytes, szBytes);
    const size_t expected_size = 2*szBytes;

    for (int i = 1; i < nFiles; ++i) {
        size_t filesz, filesz_old;
        auto buf = pvt2.mapFile(i, Blocks::ForwardBlock, &filesz);
        auto buf_old = pvt.mapFile(i, Blocks::ForwardBlock, &filesz_old);
        //LogPrintf("%d got size %d expected %d, old=%d\n", i, int(filesz), int(expected_size), int(filesz_old));
        BOOST_CHECK(filesz == expected_size);
        BOOST_CHECK(filesz_old < filesz);
        BOOST_CHECK(buf.get() != nullptr && buf.get()[filesz-1] == char(i%128));
        pvt.fileHasGrown(i); // notify -- will make following tests pass..
    }

    for (int i = 1; i < nFiles; ++i) {
        size_t filesz;
        auto buf = pvt.mapFile(i, Blocks::ForwardBlock, &filesz); // should pickup *NEW* size now

        //LogPrintf("%d got size %d expected %d, old=%d\n", i, int(filesz), int(expected_size), int(filesz_old));
        BOOST_CHECK(filesz == expected_size);
        LogPrintf("%d extant buf (pointing to old size) = %p -- new buf (pointing to new size) = %p\n",
                  i,
                  reinterpret_cast<void *>(bufs[i].first.get()),
                  reinterpret_cast<void *>(buf.get()));
        BOOST_CHECK(buf.get() != nullptr && buf.get()[filesz-1] == char(i%128));
    }

}
#endif

BOOST_AUTO_TEST_CASE(mapFile_write)
{
    // There likely is one already, for the genesis block, lets avoid interacting with it and just force a new file.
    BOOST_CHECK_EQUAL(vinfoBlockFile.size(), 1);
    vinfoBlockFile[0].nSize = MAX_BLOCKFILE_SIZE - 107;

    Blocks::DB *db = Blocks::DB::instance();
    Streaming::BufferPool pool;
    pool.reserve(100);
    for (int i = 0; i < 100; ++i) {
        pool.begin()[i] = static_cast<char>(i);
    }
    FastBlock block(pool.commit(100));
    BOOST_CHECK_EQUAL(block.size(), 100);
    BOOST_CHECK_EQUAL(block.blockVersion(), 0x3020100);
    CDiskBlockPos pos;
    {
        FastBlock newBlock = db->writeBlock(1, block, pos);
        BOOST_CHECK_EQUAL(newBlock.blockVersion(), 0x3020100);
        BOOST_CHECK_EQUAL(newBlock.size(), 100);
        BOOST_CHECK_EQUAL(pos.nFile, 1);
        BOOST_CHECK_EQUAL(pos.nPos, 8);
    }
    {
        FastBlock block2 = db->loadBlock(CDiskBlockPos(1, 8));
        BOOST_CHECK_EQUAL(block2.size(), 100);
        BOOST_CHECK_EQUAL(block2.blockVersion(), 0x3020100);
    }

    // add a second block
    pool.reserve(120);
    for (int i = 0; i < 120; ++i) {
        pool.begin()[i] = static_cast<char>(i + 1);
    }
    FastBlock block2(pool.commit(120));
    BOOST_CHECK_EQUAL(block2.size(), 120);
    BOOST_CHECK_EQUAL(block2.blockVersion(), 0x4030201);

    {
        FastBlock newBlock = db->writeBlock(2, block2, pos);
        BOOST_CHECK_EQUAL(newBlock.size(), 120);
        BOOST_CHECK_EQUAL(pos.nFile, 1);
        BOOST_CHECK_EQUAL(pos.nPos, 116);
        BOOST_CHECK_EQUAL(newBlock.blockVersion(), 0x4030201);
    }
    {
        FastBlock block3 = db->loadBlock(CDiskBlockPos(1, 8));
        BOOST_CHECK_EQUAL(block3.size(), 100);
        BOOST_CHECK_EQUAL(block3.blockVersion(), 0x3020100);
        BOOST_CHECK_EQUAL(block3.data().begin()[99], (char) 99);

        FastBlock block4 = db->loadBlock(CDiskBlockPos(1, 116));
        BOOST_CHECK_EQUAL(block4.size(), 120);
        BOOST_CHECK_EQUAL(block4.blockVersion(), 0x4030201);
        BOOST_CHECK(block4.data().begin()[119] == 120);
    }

    pool.reserve(1E6);
    FastBlock big = pool.commit(1E6);

    int remapLeft = BLOCKFILE_CHUNK_SIZE - 120 - 100;
    while (remapLeft > 0) {
        // at one point we will be auto-extending the file.
        db->writeBlock(5, big, pos);
        remapLeft -= big.size();
    }

    {
        FastBlock newBlock = db->writeBlock(6, block2, pos);
        BOOST_CHECK_EQUAL(newBlock.size(), 120);
        BOOST_CHECK_EQUAL(newBlock.blockVersion(), 0x4030201);
    }
}

BOOST_AUTO_TEST_CASE(mapFile_writeUndo)
{
    BOOST_CHECK_EQUAL(vinfoBlockFile.size(), 1);
    vinfoBlockFile[0].nSize = MAX_BLOCKFILE_SIZE - 107;

    Blocks::DB *db = Blocks::DB::instance();
    Streaming::BufferPool pool;

    CBlockUndo undoBlock;
    CTxInUndo in = { { 10, CScript() }, false, 10, 3 };
    CTxUndo tx;
    tx.vprevout.push_back(in);
    undoBlock.vtxundo.push_back(tx);

    FastUndoBlock block = FastUndoBlock::fromOldBlock(undoBlock);

    uint256 random("0x3102389012829081203809128324729384712931203892379023802183017083");
    BOOST_CHECK_EQUAL(block.size(), 6);
    {
        uint32_t pos;
        FastUndoBlock newBlock = db->writeUndoBlock(block, random, 0, &pos);
        BOOST_CHECK_EQUAL(newBlock.size(), 6);
        BOOST_CHECK_EQUAL(pos, 8);
    }
    {
        FastUndoBlock newBlock = db->loadUndoBlock(CDiskBlockPos(0, 8), random);
        BOOST_CHECK_EQUAL(newBlock.size(), 6);
        CBlockUndo block2 = newBlock.createOldBlock();
        BOOST_CHECK_EQUAL(block2.vtxundo.size(), 1);
        BOOST_CHECK_EQUAL(block2.vtxundo[0].vprevout.size(), 1);
        BOOST_CHECK_EQUAL(block2.vtxundo[0].vprevout[0].nVersion, 3);
    }
}

BOOST_AUTO_TEST_SUITE_END()
