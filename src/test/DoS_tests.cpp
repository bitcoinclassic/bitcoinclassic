// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2015-2016 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Unit tests for denial-of-service detection/prevention code

#include "chainparams.h"
#include "keystore.h"
#include "main.h"
#include "net.h"
#include "pow.h"
#include "script/sign.h"
#include "serialize.h"
#include "util.h"
#include  <txorphancache.h>

#include "test/test_bitcoin.h"

#include <stdint.h>

#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/foreach.hpp>
#include <boost/test/unit_test.hpp>

CService ip(uint32_t i)
{
    struct in_addr s;
    s.s_addr = i;
    return CService(CNetAddr(s), Params().GetDefaultPort());
}

class OrphanCacheMock : public CTxOrphanCache
{
public:
    std::map<uint256, COrphanTx> mapOrphanTransactions() {
        return m_mapOrphanTransactions;
    }
    std::map<uint256, std::set<uint256> > mapOrphanTransactionsByPrev() {
        return m_mapOrphanTransactionsByPrev;
    }

   void LimitOrphanTxSizePublic(unsigned int max) {
       LimitOrphanTxSize(max);
   }

    CTransaction RandomOrphan()
    {
        auto it = m_mapOrphanTransactions.lower_bound(GetRandHash());
        if (it == m_mapOrphanTransactions.end())
            it = m_mapOrphanTransactions.begin();
        return it->second.tx;
    }
};

BOOST_FIXTURE_TEST_SUITE(DoS_tests, TestingSetup)

BOOST_AUTO_TEST_CASE(DoS_banning)
{
    CNode::ClearBanned();
    CAddress addr1(ip(0xa0b0c001));
    CNode dummyNode1(INVALID_SOCKET, addr1, "", true);
    dummyNode1.nVersion = 1;
    Misbehaving(dummyNode1.GetId(), 100); // Should get banned
    SendMessages(&dummyNode1);
    BOOST_CHECK(CNode::IsBanned(addr1));
    BOOST_CHECK(!CNode::IsBanned(ip(0xa0b0c001|0x0000ff00))); // Different IP, not banned

    CAddress addr2(ip(0xa0b0c002));
    CNode dummyNode2(INVALID_SOCKET, addr2, "", true);
    dummyNode2.nVersion = 1;
    Misbehaving(dummyNode2.GetId(), 50);
    SendMessages(&dummyNode2);
    BOOST_CHECK(!CNode::IsBanned(addr2)); // 2 not banned yet...
    BOOST_CHECK(CNode::IsBanned(addr1));  // ... but 1 still should be
    Misbehaving(dummyNode2.GetId(), 50);
    SendMessages(&dummyNode2);
    BOOST_CHECK(CNode::IsBanned(addr2));
}

BOOST_AUTO_TEST_CASE(DoS_banscore)
{
    CNode::ClearBanned();
    mapArgs["-banscore"] = "111"; // because 11 is my favorite number
    CAddress addr1(ip(0xa0b0c001));
    CNode dummyNode1(INVALID_SOCKET, addr1, "", true);
    dummyNode1.nVersion = 1;
    Misbehaving(dummyNode1.GetId(), 100);
    SendMessages(&dummyNode1);
    BOOST_CHECK(!CNode::IsBanned(addr1));
    Misbehaving(dummyNode1.GetId(), 10);
    SendMessages(&dummyNode1);
    BOOST_CHECK(!CNode::IsBanned(addr1));
    Misbehaving(dummyNode1.GetId(), 1);
    SendMessages(&dummyNode1);
    BOOST_CHECK(CNode::IsBanned(addr1));
    mapArgs.erase("-banscore");
}

BOOST_AUTO_TEST_CASE(DoS_bantime)
{
    CNode::ClearBanned();
    int64_t nStartTime = GetTime();
    SetMockTime(nStartTime); // Overrides future calls to GetTime()

    CAddress addr(ip(0xa0b0c001));
    CNode dummyNode(INVALID_SOCKET, addr, "", true);
    dummyNode.nVersion = 1;

    Misbehaving(dummyNode.GetId(), 100);
    SendMessages(&dummyNode);
    BOOST_CHECK(CNode::IsBanned(addr));

    SetMockTime(nStartTime+60*60);
    BOOST_CHECK(CNode::IsBanned(addr));

    SetMockTime(nStartTime+60*60*24+1);
    BOOST_CHECK(!CNode::IsBanned(addr));
}


BOOST_AUTO_TEST_CASE(DoS_mapOrphans)
{
    CKey key;
    key.MakeNewKey(true);
    CBasicKeyStore keystore;
    keystore.AddKey(key);

    OrphanCacheMock cache;

    // 50 orphan transactions:
    for (int i = 0; i < 50; i++)
    {
        CMutableTransaction tx;
        tx.vin.resize(1);
        tx.vin[0].prevout.n = 0;
        tx.vin[0].prevout.hash = GetRandHash();
        tx.vin[0].scriptSig << OP_1;
        tx.vout.resize(1);
        tx.vout[0].nValue = 1*CENT;
        tx.vout[0].scriptPubKey = GetScriptForDestination(key.GetPubKey().GetID());
        cache.AddOrphanTx(tx, i);
    }

    // ... and 50 that depend on other orphans:
    for (int i = 0; i < 50; i++)
    {
        CTransaction txPrev = cache.RandomOrphan();

        CMutableTransaction tx;
        tx.vin.resize(1);
        tx.vin[0].prevout.n = 0;
        tx.vin[0].prevout.hash = txPrev.GetHash();
        tx.vout.resize(1);
        tx.vout[0].nValue = 1*CENT;
        tx.vout[0].scriptPubKey = GetScriptForDestination(key.GetPubKey().GetID());
        SignSignature(keystore, txPrev, tx, 0);

        cache.AddOrphanTx(tx, i);
    }

    // This really-big orphan should be ignored:
    for (int i = 0; i < 10; i++)
    {
        CTransaction txPrev = cache.RandomOrphan();

        CMutableTransaction tx;
        tx.vout.resize(1);
        tx.vout[0].nValue = 1*CENT;
        tx.vout[0].scriptPubKey = GetScriptForDestination(key.GetPubKey().GetID());
        tx.vin.resize(500);
        for (unsigned int j = 0; j < tx.vin.size(); j++)
        {
            tx.vin[j].prevout.n = j;
            tx.vin[j].prevout.hash = txPrev.GetHash();
        }
        SignSignature(keystore, txPrev, tx, 0);
        // Re-use same signature for other inputs
        // (they don't have to be valid for this test)
        for (unsigned int j = 1; j < tx.vin.size(); j++)
            tx.vin[j].scriptSig = tx.vin[0].scriptSig;

        if (i == 0) {
            BOOST_CHECK(cache.AddOrphanTx(tx, i));  // we keep orphans up to the configured memory limit to help xthin compression so this should succeed whereas it fails in other clients
        }
    }

    // Test LimitOrphanTxSize() function:
    {
        cache.LimitOrphanTxSizePublic(40);
        BOOST_CHECK(cache.mapOrphanTransactions().size() <= 40);
        cache.LimitOrphanTxSizePublic(10);
        BOOST_CHECK(cache.mapOrphanTransactions().size() <= 10);
        cache.LimitOrphanTxSizePublic(0);
        BOOST_CHECK(cache.mapOrphanTransactions().empty());
        BOOST_CHECK(cache.mapOrphanTransactionsByPrev().empty());
    }

    // Test EraseOrphansByTime():
    {
        int64_t nStartTime = GetTime();
        SetMockTime(nStartTime); // Overrides future calls to GetTime()
        for (int i = 0; i < 50; i++)
        {
            CMutableTransaction tx;
            tx.vin.resize(1);
            tx.vin[0].prevout.n = 0;
            tx.vin[0].prevout.hash = GetRandHash();
            tx.vin[0].scriptSig << OP_1;
            tx.vout.resize(1);
            tx.vout[0].nValue = 1*CENT;
            tx.vout[0].scriptPubKey = GetScriptForDestination(key.GetPubKey().GetID());

            cache.AddOrphanTx(tx, i);
        }
        BOOST_CHECK(cache.mapOrphanTransactions().size() == 50);
        cache.EraseOrphansByTime();
        BOOST_CHECK(cache.mapOrphanTransactions().size() == 50);

        // Advance the clock 1 minute
        SetMockTime(nStartTime+60);
        cache.EraseOrphansByTime();
        BOOST_CHECK(cache.mapOrphanTransactions().size() == 50);

        // Advance the clock 10 minutes
        SetMockTime(nStartTime+60*10);
        cache.EraseOrphansByTime();
        BOOST_CHECK(cache.mapOrphanTransactions().size() == 50);

        // Advance the clock 1 hour
        SetMockTime(nStartTime+60*60);
        cache.EraseOrphansByTime();
        BOOST_CHECK(cache.mapOrphanTransactions().size() == 50);

        // Advance the clock 72 hours
        SetMockTime(nStartTime+60*60*72);
        cache.EraseOrphansByTime();
        BOOST_CHECK(cache.mapOrphanTransactions().size() == 50);

        /** Test the boundary where orphans should get purged. **/
        // Advance the clock 72 hours and 4 minutes 59 seconds
        SetMockTime(nStartTime+60*60*72 + 299);
        cache.EraseOrphansByTime();
        BOOST_CHECK(cache.mapOrphanTransactions().size() == 50);

        // Advance the clock 72 hours and 5 minutes
        SetMockTime(nStartTime+60*60*72 + 300);
        cache.EraseOrphansByTime();
        BOOST_CHECK(cache.mapOrphanTransactions().size() == 0);
    }
}

BOOST_AUTO_TEST_SUITE_END()
