# SILVR — Silver Protocol

> "2026/01/01 Silver: The People's Chain"

## Current Status — Pre-Mainnet

Single node mining. Blocks are real. Chain is real.
True mainnet requires 3 nodes and transaction signing.
We are building in public. No lies.

| Detail | Value |
|--------|-------|
| Genesis Block | March 16, 2026 |
| Chain ID | 2026 |
| Ticker | SILVR |
| Max Supply | 42,000,000 SILVR |
| Block Reward | 50 SILVR |
| Blocks Mined | 70,000+ |
| Algorithm | SHA-256d |
| Twitter | @SILVRprotocol |

## What is SILVR

Custom Layer-1 blockchain built in C from scratch.
Not a fork. Not a copy.
No ICO. No presale. No VC.

## Create Your SILVR Wallet

Step 1 — Install dependencies:
sudo apt install gcc libssl-dev libsecp256k1-dev make git -y

Step 2 — Clone SILVR:
git clone https://github.com/him-SILVR/SILVR.git
cd SILVR

Step 3 — Build wallet:
gcc src/wallet/wallet.c -o silvr_wallet -lssl -lcrypto -lsecp256k1 -Iinclude

Step 4 — Create your wallet:
./silvr_wallet

You get your SILVR address and private key.
Save private key — never share it.

Step 5 — Build and start mining:
make
./silvrd YOUR_ADDRESS_HERE

## Check Your Balance

him-silvr.github.io/SILVR/explorer.html

## Roadmap

- Genesis block mined March 16 2026 — DONE
- Code public on GitHub — DONE
- Single node running — DONE
- Wallet generator — DONE
- Block explorer live — DONE
- Real transaction signing — NEXT
- Second node connected — NEXT
- True mainnet 3 nodes — COMING

## Community

Twitter: https://twitter.com/SILVRprotocol
Telegram: SILVR Official
GitHub: https://github.com/him-SILVR/SILVR

## License

MIT — Open source forever
### For Windows Users (MSYS2)

1. Install MSYS2 from https://www.msys2.org
2. Open MSYS2 terminal
3. Navigate to folder: `cd /e/SILVR`
4. Build: `make`
5. Run: `./silvrd.exe YOUR_ADDRESS`

