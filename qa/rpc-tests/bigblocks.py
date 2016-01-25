#!/usr/bin/env python2
# Copyright (c) 2014 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

#
# Test mining and broadcast of larger-than-1MB-blocks
#
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *

from decimal import Decimal

CACHE_DIR = "cache_bigblock"

# regression test / testnet fork params:
FORK_TIME = 1393632000
FORK_EXPIRATION = 1514764800
FORK_BLOCK_BIT = 0x10000000
FORK_GRACE_PERIOD = 60*60*24

class BigBlockTest(BitcoinTestFramework):

    def setup_chain(self):
        print("Initializing test directory "+self.options.tmpdir)
        print("Be patient, this test can take 2 or more minutes to run.")

        if not os.path.isdir(os.path.join(CACHE_DIR, "node0")):
            print("Creating initial chain")

            for i in range(4):
                initialize_datadir(CACHE_DIR, i) # Overwrite port/rpcport in bitcoin.conf

            first_block_time = FORK_TIME - 200 * 10*60

            # Node 0 tries to create as-big-as-possible blocks.
            # Node 1 creates really small, old-version blocks
            # Node 2 creates empty up-version blocks
            # Node 3 creates empty, old-version blocks
            self.nodes = []
            # Use node0 to mine blocks for input splitting
            self.nodes.append(start_node(0, CACHE_DIR, ["-blockmaxsize=2000000", "-debug=net",
                                                        "-mocktime=%d"%(first_block_time,),
                                                        "-vote2mb=1"]))
            self.nodes.append(start_node(1, CACHE_DIR, ["-blockmaxsize=50000", "-debug=net",
                                                        "-mocktime=%d"%(first_block_time,),
                                                        "-vote2mb=0"]))
            self.nodes.append(start_node(2, CACHE_DIR, ["-blockmaxsize=1000",
                                                        "-mocktime=%d"%(first_block_time,),
                                                        "-vote2mb=1"]))
            self.nodes.append(start_node(3, CACHE_DIR, ["-blockmaxsize=1000",
                                                        "-mocktime=%d"%(first_block_time,),
                                                        "-vote2mb=0"]))

            set_node_times(self.nodes, first_block_time)

            connect_nodes_bi(self.nodes, 0, 1)
            connect_nodes_bi(self.nodes, 1, 2)
            connect_nodes_bi(self.nodes, 2, 3)
            connect_nodes_bi(self.nodes, 3, 0)

            self.is_network_split = False
            self.sync_all()

            # Have node0 and node1 alternate finding blocks
            # before the fork time, so it's 50% / 50% vote
            block_time = first_block_time
            for i in range(0,200):
                miner = i%2
                set_node_times(self.nodes, block_time)
                b1hash = self.nodes[miner].generate(1)[0]
                b1 = self.nodes[miner].getblock(b1hash, True)
                if miner % 2: assert(not (b1['version'] & FORK_BLOCK_BIT))
                else: assert(b1['version'] & FORK_BLOCK_BIT)
                assert(self.sync_blocks(self.nodes[0:2]))
                block_time = block_time + 10*60

            # Generate 1200 addresses
            addresses = [ self.nodes[3].getnewaddress() for i in range(0,1200) ]

            amount = Decimal("0.00125")

            send_to = { }
            for address in addresses:
                send_to[address] = amount

            tx_file = open(os.path.join(CACHE_DIR, "txdata"), "w")

            # Create four megabytes worth of transactions ready to be
            # mined:
            print("Creating 100 40K transactions (4MB)")
            for node in range(0,2):
                for i in range(0,50):
                    txid = self.nodes[node].sendmany("", send_to, 1)
                    txdata = self.nodes[node].getrawtransaction(txid)
                    tx_file.write(txdata+"\n")
            tx_file.close()

            stop_nodes(self.nodes)
            wait_bitcoinds()
            self.nodes = []
            for i in range(4):
                os.remove(log_filename(CACHE_DIR, i, "debug.log"))
                os.remove(log_filename(CACHE_DIR, i, "db.log"))
                os.remove(log_filename(CACHE_DIR, i, "peers.dat"))
                os.remove(log_filename(CACHE_DIR, i, "fee_estimates.dat"))


        for i in range(4):
            from_dir = os.path.join(CACHE_DIR, "node"+str(i))
            to_dir = os.path.join(self.options.tmpdir,  "node"+str(i))
            shutil.copytree(from_dir, to_dir)
            initialize_datadir(self.options.tmpdir, i) # Overwrite port/rpcport in bitcoin.conf

    def sync_blocks(self, rpc_connections, wait=0.1, max_wait=30):
        """
        Wait until everybody has the same block count
        """
        for i in range(0,max_wait):
            if i > 0: time.sleep(wait)
            counts = [ x.getblockcount() for x in rpc_connections ]
            if counts == [ counts[0] ]*len(counts):
                return True
        return False

    def setup_network(self):
        self.nodes = []
        last_block_time = FORK_TIME - 10*60

        self.nodes.append(start_node(0, self.options.tmpdir, ["-blockmaxsize=2000000", "-debug=net",
                                                              "-mocktime=%d"%(last_block_time,),
                                                              "-vote2mb=1"]))
        self.nodes.append(start_node(1, self.options.tmpdir, ["-blockmaxsize=50000", "-debug=net",
                                                              "-mocktime=%d"%(last_block_time,),
                                                              "-vote2mb=0"]))
        self.nodes.append(start_node(2, self.options.tmpdir, ["-blockmaxsize=1000",
                                                              "-mocktime=%d"%(last_block_time,),
                                                              "-vote2mb=1"]))
        self.nodes.append(start_node(3, self.options.tmpdir, ["-blockmaxsize=1000",
                                                              "-mocktime=%d"%(last_block_time,),
                                                              "-vote2mb=0"]))
        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 1, 2)
        connect_nodes_bi(self.nodes, 2, 3)
        connect_nodes_bi(self.nodes, 3, 0)

        # Populate node0's mempool with cached pre-created transactions:
        with open(os.path.join(CACHE_DIR, "txdata"), "r") as f:
            for line in f:
                self.nodes[0].sendrawtransaction(line.rstrip())

    def copy_mempool(self, from_node, to_node):
        txids = from_node.getrawmempool()
        for txid in txids:
            txdata = from_node.getrawtransaction(txid)
            to_node.sendrawtransaction(txdata)

    def TestMineBig(self, expect_big, expect_version=None):
        # Test if node0 will mine big blocks.
        b1hash = self.nodes[0].generate(1)[0]
        b1 = self.nodes[0].getblock(b1hash, True)
        assert(self.sync_blocks(self.nodes))

        if expect_version:
            assert b1['version'] & FORK_BLOCK_BIT
        elif not expect_version==None:
            assert not b1['version'] & FORK_BLOCK_BIT

        if expect_big:
            assert(b1['size'] > 1000*1000)

            # Have node1 mine on top of the block,
            # to make sure it goes along with the fork
            b2hash = self.nodes[1].generate(1)[0]
            b2 = self.nodes[1].getblock(b2hash, True)
            assert(b2['previousblockhash'] == b1hash)
            assert(self.sync_blocks(self.nodes))

        else:
            assert(b1['size'] <= 1000*1000)

        # Reset chain to before b1hash:
        for node in self.nodes:
            node.invalidateblock(b1hash)
        assert(self.sync_blocks(self.nodes))


    def run_test(self):
        # nodes 0 and 1 have 50 mature 50-BTC coinbase transactions.
        # Spend them with 50 transactions, each that has
        # 1,200 outputs (so they're about 41K big).

        print("Testing fork conditions")

        # Fork is controlled by block timestamp and miner super-majority;
        # large blocks may only be created after a supermajority of miners
        # produce up-version blocks plus a grace period

        # At this point the chain is 200 blocks long
        # alternating between version=0x20000000 and version=0x30000000
        # blocks.

        # Nodes will ignore their -vote2mb settings under a few situations.
        # First, during the grace period, all nodes will vote for the fork.
        # Second, after activation, all nodes will cease voting for the fork,
        # to allow the 0x10000000 bit to be recycled for other forks. Third,
        # blocks after the expiration date will not vote for the fork, and
        # may not activate it, 

        # NOTE: the order of these test is important!
        # set_node_times must advance time. Local time moving
        # backwards causes problems.

        # Time starts a little before fork activation time:
        set_node_times(self.nodes, FORK_TIME - 100)

        # No supermajority, vote2mb=1
        self.TestMineBig(False, True)

        # Create a block after the expiration date. This will be rejected 
        # by the other nodes for being more than 2 hours in the future,
        # and will have FORK_BLOCK_BIT cleared.

        set_node_times(self.nodes[0:1], FORK_EXPIRATION + 100)

        b1hash = self.nodes[0].generate(1)[0]
        b1 = self.nodes[0].getblock(b1hash, True)
        assert(not (b1['version'] & FORK_BLOCK_BIT))
        self.nodes[0].invalidateblock(b1hash)
        set_node_times(self.nodes[0:1], FORK_TIME - 100)
        assert(self.sync_blocks(self.nodes))


        # node2 creates empty up-version blocks; creating
        # 50 in a row makes 75 of previous 100 up-version
        # (which is the -regtest activation condition)
        t_delta = FORK_GRACE_PERIOD/50
        blocks = []
        for i in range(50):
            set_node_times(self.nodes, FORK_TIME + t_delta*i - 1)
            blocks.append(self.nodes[2].generate(1)[0])
        assert(self.sync_blocks(self.nodes))

        # Now that we have a supermajority, node3 should ignore its -vote2mb=0
        # setting and indicate that it is technically compatible with the fork

        b1hash = self.nodes[3].generate(1)[0]
        b1 = self.nodes[3].getblock(b1hash, True)
        assert(b1['version'] & FORK_BLOCK_BIT)
        assert(self.sync_blocks(self.nodes))
        for node in self.nodes: node.invalidateblock(b1hash)
        assert(self.sync_blocks(self.nodes))

        # Earliest time for a big block is the timestamp of the
        # supermajority block plus grace period:
        lastblock = self.nodes[0].getblock(blocks[-1], True)
        t_fork = lastblock["time"] + FORK_GRACE_PERIOD

        self.TestMineBig(False, True)  # Supermajority... but before grace period end

        # Test right around the switchover time.
        set_node_times(self.nodes, t_fork-1)
        self.TestMineBig(False, True)

        # Note that node's local times are irrelevant, block timestamps
        # are all that count-- so node0 will mine a big block with timestamp in the
        # future from the perspective of the other nodes, but as long as
        # it's timestamp is not too far in the future (2 hours) it will be
        # accepted.
        self.nodes[0].setmocktime(t_fork)
        self.TestMineBig(True, False)

        # Shutdown then restart node[0], it should
        # remember supermajority state and produce a big block.
        stop_node(self.nodes[0], 0)
        self.nodes[0] = start_node(0, self.options.tmpdir, ["-blockmaxsize=2000000", "-debug=net",
                                                            "-mocktime=%d"%(t_fork,),
                                                            "-vote2mb=1"])
        self.copy_mempool(self.nodes[1], self.nodes[0])
        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 0, 3)
        self.TestMineBig(True, False)

        # Test re-orgs past the activation block (blocks[-1])
        #
        # Shutdown node[0] again:
        stop_node(self.nodes[0], 0)

        # Mine a longer chain with two version=4 blocks:
        self.nodes[3].invalidateblock(blocks[-1])
        v3blocks = self.nodes[3].generate(2)
        assert(self.sync_blocks(self.nodes[1:]))

        # Restart node0, it should re-org onto longer chain, reset
        # activation time, and refuse to mine a big block:
        self.nodes[0] = start_node(0, self.options.tmpdir, ["-blockmaxsize=2000000", "-debug=net",
                                                            "-mocktime=%d"%(t_fork,),
                                                            "-vote2mb=1"])
        self.copy_mempool(self.nodes[1], self.nodes[0])
        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 0, 3)
        assert(self.sync_blocks(self.nodes))
        self.TestMineBig(False, True)

        # Mine 4 FORK_BLOCK_BIT blocks and set the time past the
        # grace period:  bigger block OK:
        self.nodes[2].generate(4)
        assert(self.sync_blocks(self.nodes))
        set_node_times(self.nodes, t_fork + FORK_GRACE_PERIOD)
        self.TestMineBig(True, False)


        print("Cached test chain and transactions left in %s"%(CACHE_DIR))
        print(" (remove that directory if you will not run this test again)")


if __name__ == '__main__':
    BigBlockTest().main()
