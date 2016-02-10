#!/bin/bash

CONFIG_PATH=$HOME/.bitcoin/bitcoin.conf

TESTNET=${TESTNET:-0}
RPCUSER=${RPCUSER:-rpcuser}
RPCPASSWORD=${RPCPASSWORD:-$(dd if=/dev/random bs=33 count=1 2>/dev/null | base64)}
RPCALLOWIP=${RPCALLOWIP:-"127.0.0.1/32"}

read -r -d '' CONFIG <<-EOF
	testnet=$TESTNET
	disablewallet=1
	rpcuser=$RPCUSER
	rpcpassword=$RPCPASSWORD
	rpcallowip=$RPCALLOWIP
EOF

mkdir -p $HOME/.bitcoin
if [ -n $CONFIG_PATH ]; then
  if [ -e /bitcoin/bitcoin.conf ]; then
    cp /bitcoin/bitcoin.conf $CONFIG_PATH
  else
    echo "$CONFIG" > $CONFIG_PATH
  fi
  chmod 0600 $CONFIG_PATH
fi

$HOME/bitcoin-0.11.2/bin/bitcoind
