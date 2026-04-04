# SILVR — Silver Protocol

> "2026/01/01 Silver: The People's Chain"

## STATUS: Pre-Mainnet — Single Node Mining

Genesis Block: March 16, 2026
Chain ID: 2026
Ticker: SILVR
Max Supply: 42,000,000 SILVR
Block Reward: 50 SILVR
Algorithm: SHA-256d
Twitter: @SILVRprotocol

## What is SILVR

Custom Layer-1 blockchain built in C from scratch.
Not a fork. Not a copy.
No ICO. No presale. No VC.

## Build and Run
## Create Your SILVR Wallet

Step 1 — Install dependencies:
sudo apt install gcc libssl-dev libsecp256k1-dev make git -y

Step 2 — Clone SILVR:
git clone https://github.com/him-SILVR/SILVR.git
cd SILVR

Step 3 — Build wallet generator:
gcc src/wallet/wallet.c -o silvr_wallet -lssl -lcrypto -lsecp256k1 -Iinclude

Step 4 — Create your wallet:
./silvr_wallet

This prints your:
- SILVR Address (starts with S)
- Private Key (save this — never share)

Step 5 — Start mining with your address:
./silvrd YOUR_ADDRESS_HERE

Example:
./silvrd SWLswgMRtZ8hn2VHxtJ4EJX46C4fKXDWrE


sudo apt install gcc libssl-dev make
git clone https://github.com/him-SILVR/SILVR.git
cd SILVR
make
./silvrd

## Roadmap

- Genesis block mined March 16 2026 DONE
- Code public on GitHub DONE
- Single node running DONE
- Real transaction signing NEXT
- BIP-39 wallet recovery NEXT
- Second node connected NEXT
- Block explorer NEXT
- True mainnet 3 nodes COMING

## Community

Twitter: https://twitter.com/SILVRprotocol
Telegram: SILVR Official


