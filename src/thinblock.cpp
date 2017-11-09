// Copyright (c) 2016 The Bitcoin Unlimited developers
// Copyright (c) 2016-2017 Tom Zander <tomz@freedommail.ch>
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "thinblock.h"
#include <sstream>
#include <iomanip>

#include "main.h"
#include "chainparams.h"
#include "txmempool.h"
#include "BlocksDB.h"
#include "utilstrencodings.h"
#include "txorphancache.h"
#include "consensus/merkle.h"
#include "consensus/validation.h"
#include "policy/policy.h"

#include <boost/foreach.hpp>
#include <boost/thread.hpp>
#include <boost/lexical_cast.hpp>

std::map<uint256, uint64_t> mapThinBlockTimer;

std::vector<CNode*> xpeditedBlk; // Who requested expedited blocks from us
std::vector<CNode*> xpeditedBlkUp; // Who we requested expedited blocks from
std::vector<CNode*> xpeditedTxn;

CXThinBlock::CXThinBlock(const CBlock& block, CBloomFilter* filter)
    : collision(false)
{
    header = block.GetBlockHeader();

    unsigned int nTx = block.vtx.size();
    vTxHashes.reserve(nTx);
    std::set<uint64_t> setPartialTxHash;
    for (unsigned int i = 0; i < nTx; i++) {
        const uint256 hash256 = block.vtx[i].GetHash();
        const uint64_t cheapHash = hash256.GetCheapHash();
        vTxHashes.push_back(cheapHash);

        if (collision || setPartialTxHash.count(cheapHash))
            collision = true;
        setPartialTxHash.insert(cheapHash);

        // Find the transactions that do not match the filter.
        // These are the ones we need to relay back to the requesting peer.
        // NOTE: We always add the first tx, the coinbase as it is the one
        //       most often missing.
        if (i == 0 || (filter && !filter->contains(hash256)))
            vMissingTx.push_back(block.vtx[i]);
    }
}

CXThinBlock::CXThinBlock()
    : collision(false)
{
}

bool CXThinBlock::process(CNode* pfrom)
{
    pfrom->thinBlock = CBlock(header);
    pfrom->xThinBlockHashes = vTxHashes;

    // Create the mapMissingTx from all the supplied tx's in the xthinblock
    std::map<uint64_t, CTransaction> mapMissingTx;
    BOOST_FOREACH(CTransaction tx, vMissingTx) {
        mapMissingTx[tx.GetHash().GetCheapHash()] = tx;
    }

    std::map<uint64_t, uint256> orphanLookup;
    {
        auto orphans = CTxOrphanCache::instance()->mapOrphanTransactions();
        BOOST_FOREACH (auto iter, orphans) {
            orphanLookup.insert(std::make_pair(iter.first.GetCheapHash(), iter.first));
        }
    }

    std::map<uint64_t, uint256> mempoolLookup;
    {
        std::vector<uint256> memPoolHashes;
        mempool.queryHashes(memPoolHashes);

        for (size_t i = 0; i < memPoolHashes.size(); ++i) {
            const uint256 &hash = memPoolHashes.at(i);
            mempoolLookup.insert(std::make_pair(hash.GetCheapHash(), hash));
        }
    }

    std::vector<uint256> orphansUsed;
    int missingCount = 0;
    int collisionCount = 0;
    std::uint32_t blockSize = ::GetSerializeSize(pfrom->thinBlock, SER_NETWORK, PROTOCOL_VERSION);
    const std::uint32_t blockSizeAcceptLimit = Policy::blockSizeAcceptLimit();
    {
        LOCK2(cs_main, mempool.cs);
        const bool isChainTip = (header.hashPrevBlock == chainActive.Tip()->GetBlockHash()) ? true : false;
        for (size_t i = 0; i < vTxHashes.size(); ++i) {
            const uint64_t cheapHash = vTxHashes.at(i);
            // Now we find the full transaction.
            CTransaction tx;

            auto foundInMissing = mapMissingTx.find(cheapHash);
            if (foundInMissing != mapMissingTx.end()) {
                tx = foundInMissing->second;
            }

            auto foundInMempool = mempoolLookup.find(cheapHash);
            if (foundInMempool != mempoolLookup.end()) {
                if (tx.IsNull()) {
                    if (isChainTip) // only skip validation if we are constructing the new chaintip
                        setPreVerifiedTxHash.insert(foundInMempool->second);

                    /*bool found =*/ mempool.lookup(foundInMempool->second, tx);
                } else {
                    ++collisionCount;
                }
            }

            auto foundInOrphan = orphanLookup.find(cheapHash);
            if (foundInOrphan != orphanLookup.end()) {
                if (tx.IsNull()) {
                    bool found = CTxOrphanCache::value(foundInOrphan->second, tx);
                    if (found) // a race condition may have caused it to be removed from the orphans cache
                        orphansUsed.push_back(foundInOrphan->second);
                } else {
                    ++collisionCount;
                }
            }
            blockSize += tx.GetSerializeSize(0, 0);
            if (blockSize <= blockSizeAcceptLimit)
                pfrom->thinBlock.vtx.push_back(tx);
            if (tx.IsNull())
                missingCount++;
        }
    }

    pfrom->thinBlockWaitingForTxns = missingCount; // TODO can that variable be removed from the CNode?

    if (blockSize > blockSizeAcceptLimit) {
        pfrom->thinBlockWaitingForTxns = -1;
        pfrom->thinBlock.vtx.clear();
        const float punishment = (blockSize - blockSizeAcceptLimit) / (float) blockSizeAcceptLimit;
        const int score = 10 * punishment + 0.5;
        LogPrintf("thinblock (partially) reconstructed is over accept limits; (%d > %d), Dropping block and punishing (%d) peer %d\n",
                blockSize, blockSizeAcceptLimit, score, pfrom->id);
        pfrom->mapThinBlocksInFlight.erase(pfrom->thinBlock.GetHash());
        LOCK(cs_main);
        Network::misbehaving(pfrom->id, score);
        return false;
    }

    if (missingCount == 0) {
        bool mutated;
        uint256 hashMerkleRoot2 = BlockMerkleRoot(pfrom->thinBlock, &mutated);
        if (pfrom->thinBlock.hashMerkleRoot != hashMerkleRoot2) {
            LogPrint("thin", "thinblock fully constructed, but merkle hash failed. Rejecting\n");
            // If we hit this often, we should consider writing more code above to remember duplicate
            // short hashes in our maps and also remember duplicate results from the different sources
            // like the orphans and the mempool etc.
            // With all these options we can then try different combinations and see which one gives us a proper merkle root.

            // We'll wait for an INV to get this block, rejecting it for now.
            pfrom->thinBlockWaitingForTxns = -1;
            return false;
        }
    }

    LogPrint("thin", "thinblock waiting for: %d, txs: %d full: %d\n", pfrom->thinBlockWaitingForTxns, pfrom->thinBlock.vtx.size(), mapMissingTx.size());
    if (missingCount == 0) {
        // We have all the transactions now that are in this block: try to reassemble and process.
        pfrom->thinBlockWaitingForTxns = -1;
        pfrom->AddInventoryKnown(GetInv());
        CTxOrphanCache::instance()->EraseOrphans(orphansUsed);
        return true;
    }
    // This marks the end of the transactions we've received. If we get this and we have NOT been able to
    // finish reassembling the block, we need to re-request the transactions we're missing:
    std::set<uint64_t> setHashesToRequest;
    for (size_t i = 0; i < pfrom->thinBlock.vtx.size(); i++) {
        if (pfrom->thinBlock.vtx[i].IsNull())
            setHashesToRequest.insert(pfrom->xThinBlockHashes[i]);
    }

    // Re-request transactions that we are still missing
    CXRequestThinBlockTx thinBlockTx(header.GetHash(), setHashesToRequest);
    pfrom->PushMessage(NetMsgType::GET_XBLOCKTX, thinBlockTx);
    LogPrint("thin", "Missing %d transactions for xthinblock, re-requesting\n", pfrom->thinBlockWaitingForTxns);

    return false;
}

CXThinBlockTx::CXThinBlockTx(uint256 blockHash, std::vector<CTransaction>& vTx)
{
    blockhash = blockHash;
    vMissingTx = vTx;
}

CXRequestThinBlockTx::CXRequestThinBlockTx(uint256 blockHash, std::set<uint64_t>& setHashesToRequest)
{
    blockhash = blockHash;
    setCheapHashesToRequest = setHashesToRequest;
}

bool HaveThinblockNodes()
{
    LOCK(cs_vNodes);
    BOOST_FOREACH (CNode* pnode, vNodes) {
        if (pnode->ThinBlockCapable())
            return true;
    }
    return false;
}

bool CheckThinblockTimer(const uint256 &hash)
{
    if (!mapThinBlockTimer.count(hash)) {
        mapThinBlockTimer[hash] = GetTimeMillis();
        LogPrint("thin", "Starting Preferential Thinblock timer\n");
    }
    else {
        // Check that we have not exceeded the 10 second limit.
        // If we have then we want to return false so that we can
        // proceed to download a regular block instead.
        uint64_t elapsed = GetTimeMillis() - mapThinBlockTimer[hash];
        if (elapsed > 10000) {
            LogPrint("thin", "Preferential Thinblock timer exceeded - downloading regular block instead\n");
            return false;
        }
    }
    return true;
}

bool IsChainNearlySyncd()
{
    LOCK(cs_main);
    if(chainActive.Height() < pindexBestHeader->nHeight - 2)
        return false;
    return true;
}

CBloomFilter createSeededBloomFilter(const std::vector<uint256>& vOrphanHashes)
{
    LogPrint("thin", "Starting creation of bloom filter\n");
    seed_insecure_rand();
    double nBloomPoolSize = (double)mempool.mapTx.size();
    if (nBloomPoolSize > MAX_BLOOM_FILTER_SIZE / 1.8)
        nBloomPoolSize = MAX_BLOOM_FILTER_SIZE / 1.8;
    double nBloomDecay = 1.5 - (nBloomPoolSize * 1.8 / MAX_BLOOM_FILTER_SIZE);  // We should never go below 0.5 as we will start seeing re-requests for tx's
    int nElements = std::max((int)(((int)mempool.mapTx.size() + (int)vOrphanHashes.size()) * nBloomDecay), 1); // Must make sure nElements is greater than zero or will assert
    double nFPRate = .001 + (((double)nElements * 1.8 / MAX_BLOOM_FILTER_SIZE) * .004); // The false positive rate in percent decays as the mempool grows
    CBloomFilter filterMemPool(nElements, nFPRate, insecure_rand(), BLOOM_UPDATE_ALL);
    LogPrint("thin", "Bloom multiplier: %f FPrate: %f Num elements in bloom filter: %d num mempool entries: %d\n", nBloomDecay, nFPRate, nElements, (int)mempool.mapTx.size());

    // Seed the filter with the transactions in the memory pool
    LOCK(cs_main);
    std::vector<uint256> vMemPoolHashes;
    mempool.queryHashes(vMemPoolHashes);
    for (uint64_t i = 0; i < vMemPoolHashes.size(); i++)
         filterMemPool.insert(vMemPoolHashes[i]);
    for (uint64_t i = 0; i < vOrphanHashes.size(); i++)
         filterMemPool.insert(vOrphanHashes[i]);
    LogPrint("thin", "Created bloom filter: %d bytes\n",::GetSerializeSize(filterMemPool, SER_NETWORK, PROTOCOL_VERSION));

    return filterMemPool;
}

void LoadFilter(CNode *pfrom, CBloomFilter *filter)
{
    if (!filter->IsWithinSizeConstraints()) {
        // There is no excuse for sending a too-large filter
        LOCK(cs_main);
        Network::misbehaving(pfrom->GetId(), 100);
    } else {
        LOCK(pfrom->cs_filter);
        delete pfrom->pThinBlockFilter;
        pfrom->pThinBlockFilter = new CBloomFilter(*filter);
        pfrom->pThinBlockFilter->UpdateEmptyFull();
    }
    uint64_t nSizeFilter = ::GetSerializeSize(*pfrom->pThinBlockFilter, SER_NETWORK, PROTOCOL_VERSION);
    LogPrint("thin", "Thinblock Bloom filter size: %d\n", nSizeFilter);
}

void HandleBlockMessage(CNode *pfrom, const std::string &strCommand, const CBlock &block, const CInv &inv)
{
    int64_t startTime = GetTimeMicros();
    CValidationState state;
    // Process all blocks from whitelisted peers, even if not requested,
    // unless we're still syncing with the network.
    // Such an unrequested block may still be processed, subject to the
    // conditions in AcceptBlock().
    bool forceProcessing = pfrom->fWhitelisted && !IsInitialBlockDownload();
    const CChainParams& chainparams = Params();
    ProcessNewBlock(state, chainparams, pfrom, &block, forceProcessing, NULL);
    int nDoS;
    if (state.IsInvalid(nDoS)) {
        LogPrintf("Invalid block due to %s\n", state.GetRejectReason().c_str());
        pfrom->PushMessage("reject", strCommand, (unsigned char)state.GetRejectCode(),
                           state.GetRejectReason().substr(0, MAX_REJECT_MESSAGE_LENGTH), inv.hash);
        if (nDoS > 0) {
            LOCK(cs_main);
            Network::misbehaving(pfrom->GetId(), nDoS);
        }
    }
    LogPrint("thin", "Processed Block %s in %.2f seconds\n", inv.hash.ToString(), (double)(GetTimeMicros() - startTime) / 1000000.0);

    // When we request a thinblock we may get back a regular block if it is smaller than a thinblock
    // Therefore we have to remove the thinblock in flight if it exists and we also need to check that
    // the block didn't arrive from some other peer.  This code ALSO cleans up the thin block that
    // was passed to us (&block), so do not use it after this.
    {
        int nTotalThinBlocksInFlight = 0;
        LOCK(cs_vNodes);
        BOOST_FOREACH(CNode* pnode, vNodes) {
            if (pnode->mapThinBlocksInFlight.count(inv.hash)) {
                pnode->mapThinBlocksInFlight.erase(inv.hash);
                pnode->thinBlockWaitingForTxns = -1;
                pnode->thinBlock.SetNull();
            }
            if (pnode->mapThinBlocksInFlight.size() > 0)
                nTotalThinBlocksInFlight++;
        }

        // When we no longer have any thinblocks in flight then clear the set
        // just to make sure we don't somehow get growth over time.
        if (nTotalThinBlocksInFlight == 0) {
            setPreVerifiedTxHash.clear();
            setUnVerifiedOrphanTxHash.clear();
        }
    }

    // Clear the thinblock timer used for preferential download
    mapThinBlockTimer.erase(inv.hash);
}

void CheckAndRequestExpeditedBlocks(CNode* pfrom)
{
    if (pfrom->nVersion >= EXPEDITED_VERSION) {
        BOOST_FOREACH(std::string& strAddr, mapMultiArgs["-expeditedblock"]) {
            // Add the peer's listening port if it is empty
            int pos1 = strAddr.rfind(":");
            int pos2 = strAddr.rfind("]:");
            if (pos1 <= 0 && pos2 <= 0)
                strAddr += ':' + boost::lexical_cast<std::string>(pfrom->addrFromPort);

            std::string strListeningPeerIP;
            std::string strPeerIP = pfrom->addr.ToString();
            pos1 = strPeerIP.rfind(":");
            pos2 = strPeerIP.rfind("]:");
            // Handle both ipv4 and ipv6 cases
            if (pos1 <= 0 && pos2 <= 0) 
                strListeningPeerIP = strPeerIP + ':' + boost::lexical_cast<std::string>(pfrom->addrFromPort);
            else if (pos1 > 0)
                strListeningPeerIP = strPeerIP.substr(0, pos1) + ':' + boost::lexical_cast<std::string>(pfrom->addrFromPort);
            else
                strListeningPeerIP = strPeerIP.substr(0, pos2) + ':' + boost::lexical_cast<std::string>(pfrom->addrFromPort);

            if(strAddr == strListeningPeerIP) {
                if (!IsThinBlocksEnabled()) {
                    LogPrintf("You do not have Thinblocks enabled.  You can not request expedited blocks from peer %s (%d).\n", strListeningPeerIP, pfrom->id);
                }
                else if (!pfrom->ThinBlockCapable()) {
                    LogPrintf("Thinblocks is not enabled on remote peer.  You can not request expedited blocks from peer %s (%d).\n", strListeningPeerIP, pfrom->id);
                } else {
                    LogPrintf("Requesting expedited blocks from peer %s (%d).\n", strListeningPeerIP, pfrom->id);
                    pfrom->PushMessage(NetMsgType::XPEDITEDREQUEST, ((uint64_t) EXPEDITED_BLOCKS));
                    xpeditedBlkUp.push_back(pfrom);
                }
                return;
            }
        }
    }
}

void SendExpeditedBlock(CXThinBlock& thinBlock, unsigned char hops, const CNode* skip)
{
    std::vector<CNode*>::iterator end = xpeditedBlk.end();
    for (std::vector<CNode*>::iterator it = xpeditedBlk.begin(); it != end; it++) {
        CNode* node = *it;
        if (node && node != skip) { // Don't send it back in case there is a forwarding loop
            if (node->fDisconnect) {
                *it = nullptr;
                node->Release();
            } else {
                LogPrint("thin", "Sending expedited block %s to %s.\n", thinBlock.header.GetHash().ToString(), node->addrName.c_str());
                node->PushMessage(NetMsgType::XPEDITEDBLK, (unsigned char) EXPEDITED_MSG_XTHIN, hops, thinBlock);
                // ++node->blocksSent;
            }
        }
    }
}

void SendExpeditedBlock(const CBlock& block,const CNode* skip)
{
    if (!IsRecentlyExpeditedAndStore(block.GetHash())) {
        CXThinBlock thinBlock(block);
        SendExpeditedBlock(thinBlock,0, skip);
    }
}
void HandleExpeditedRequest(CDataStream& vRecv,CNode* pfrom)
{
    // TODO locks
    uint64_t options;
    vRecv >> options;
    bool stop = ((options & EXPEDITED_STOP) != 0);  // Are we starting or stopping expedited service?
    if (options & EXPEDITED_BLOCKS)
    {
        if (stop)  // If stopping, find the array element and clear it.
        {
            LogPrint("blk", "Stopping expedited blocks to peer %s (%d).\n", pfrom->addrName.c_str(),pfrom->id);
            std::vector<CNode*>::iterator it = std::find(xpeditedBlk.begin(), xpeditedBlk.end(),pfrom);  
            if (it != xpeditedBlk.end())
            {
                *it = NULL;
                pfrom->Release();
            }
        }
        else  // Otherwise, add the new node to the end
        {
            std::vector<CNode*>::iterator it1 = std::find(xpeditedBlk.begin(), xpeditedBlk.end(),pfrom); 
            if (it1 == xpeditedBlk.end())  // don't add it twice
            {
                unsigned int maxExpedited = GetArg("-maxexpeditedblockrecipients", 32);
                if (xpeditedBlk.size() < maxExpedited )
                {
                    LogPrint("blk", "Starting expedited blocks to peer %s (%d).\n", pfrom->addrName.c_str(),pfrom->id);
                    std::vector<CNode*>::iterator it = std::find(xpeditedBlk.begin(), xpeditedBlk.end(),((CNode*)NULL));
                    if (it != xpeditedBlk.end())
                        *it = pfrom;
                    else
                        xpeditedBlk.push_back(pfrom);
                    pfrom->AddRef();
                }
                else
                {
                    LogPrint("blk", "Expedited blocks requested from peer %s (%d), but I am full.\n", pfrom->addrName.c_str(),pfrom->id);
                }
            }
        }
    }
    if (options & EXPEDITED_TXNS)
    {
        if (stop) // If stopping, find the array element and clear it.
        {
            LogPrint("blk", "Stopping expedited transactions to peer %s (%d).\n", pfrom->addrName.c_str(),pfrom->id);
            std::vector<CNode*>::iterator it = std::find(xpeditedTxn.begin(), xpeditedTxn.end(),pfrom);
            if (it != xpeditedTxn.end())
            {
                *it = NULL;
                pfrom->Release();
            }
        }
        else // Otherwise, add the new node to the end
        {
            std::vector<CNode*>::iterator it1 = std::find(xpeditedTxn.begin(), xpeditedTxn.end(),pfrom);
            if (it1 == xpeditedTxn.end())  // don't add it twice
            {
                unsigned int maxExpedited = GetArg("-maxexpeditedtxrecipients", 32);
                if (xpeditedTxn.size() < maxExpedited )
                {
                    LogPrint("blk", "Starting expedited transactions to peer %s (%d).\n", pfrom->addrName.c_str(),pfrom->id);
                    std::vector<CNode*>::iterator it = std::find(xpeditedTxn.begin(), xpeditedTxn.end(),((CNode*)NULL));
                    if (it != xpeditedTxn.end())
                        *it = pfrom;
                    else
                        xpeditedTxn.push_back(pfrom);
                    pfrom->AddRef();
                }
                else
                {
                    LogPrint("blk", "Expedited transactions requested from peer %s (%d), but I am full.\n", pfrom->addrName.c_str(),pfrom->id);
                }
            }
        }
    }
}

// TODO make this not so ugly
#define NUM_XPEDITED_STORE 10
uint256 xpeditedBlkSent[NUM_XPEDITED_STORE];  // Just save the last few expedited sent blocks so we don't resend (uint256 zeros on construction)
int xpeditedBlkSendPos=0;

bool IsRecentlyExpeditedAndStore(const uint256& hash)
{
    for (int i=0;i<NUM_XPEDITED_STORE;i++)
        if (xpeditedBlkSent[i]==hash) return true;
    xpeditedBlkSent[xpeditedBlkSendPos] = hash;
    xpeditedBlkSendPos++;
    if (xpeditedBlkSendPos >=NUM_XPEDITED_STORE)  xpeditedBlkSendPos = 0;
    return false;
}

void HandleExpeditedBlock(CDataStream& vRecv, CNode* pfrom)
{
    unsigned char hops;
    unsigned char msgType;
    vRecv >> msgType >> hops;

    if (msgType == EXPEDITED_MSG_XTHIN) {
        CXThinBlock thinBlock;
        vRecv >> thinBlock;

        auto mapEntry = Blocks::indexMap.find(thinBlock.header.GetHash());
        CBlockIndex *blockIndex = nullptr;
        unsigned int status = 0;
        if (mapEntry != Blocks::indexMap.end()) {
            blockIndex = mapEntry->second;
            status = blockIndex->nStatus;
        }
        bool isNewBlock = blockIndex == nullptr || (!(blockIndex->nStatus & BLOCK_HAVE_DATA));  // If I have never seen the block or just seen an INV, treat the block as new

        const int nSizeThinBlock = ::GetSerializeSize(thinBlock, SER_NETWORK, PROTOCOL_VERSION);  // TODO replace with size of vRecv for efficiency
        const CInv inv(MSG_BLOCK, thinBlock.header.GetHash());
        LogPrint("thin", "Received %s expedited thinblock %s from peer %s (%d). Hop %d. Size %d bytes. (status %d,0x%x)\n", isNewBlock ? "new":"repeated",
                 inv.hash.ToString(), pfrom->addrName.c_str(), pfrom->id, hops, nSizeThinBlock, status,status);

        // Skip if we've already seen this block
        if (IsRecentlyExpeditedAndStore(thinBlock.header.GetHash()))
            return;
        if (!isNewBlock) {
            // TODO determine if we have the block or just have an INV to it.
            return;
        }

        CValidationState state;
        if (!CheckBlockHeader(thinBlock.header, state, true)) { // block header is bad
            LOCK(cs_main);
            Network::misbehaving(pfrom->id, 100);
            return;
        }
        // TODO:  Start optimistic mining now

        SendExpeditedBlock(thinBlock, hops + 1, pfrom); // I should push the vRecv rather than reserialize
        if (thinBlock.process(pfrom))
            HandleBlockMessage(pfrom, NetMsgType::XPEDITEDBLK, pfrom->thinBlock,  thinBlock.GetInv());  // clears the thin block
    } else {
        LogPrint("thin", "Received unknown (0x%x) expedited message from peer %s (%d). Hop %d.\n", msgType, pfrom->addrName.c_str(),pfrom->id, hops);
    }
}
