#!/bin/bash
set -e

echo "================================"
echo "  SILVR Node Builder"
echo "  Chain ID: 2026"
echo "================================"

echo ""
echo "Installing dependencies..."
sudo apt install -y gcc libssl-dev libsecp256k1-dev make git

echo ""
echo "Building SILVR node..."
gcc -Wall -Wextra -O2 -Iinclude \
  src/core/main.c \
  src/core/crypto.c \
  src/wallet/wallet.c \
  src/consensus/consensus.c \
  src/network/network.c \
  src/lightning/lightning.c \
  src/dao/dao.c \
  -o silvrd \
  -lssl -lcrypto -lsecp256k1 -lm

echo ""
echo "================================"
echo "  Build complete!"
echo "  Run: ./silvrd YOUR_ADDRESS"
echo "================================"
