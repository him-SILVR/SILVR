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
| Block Reward | 50 SILVR (47.5 miner + 2.5 treasury) |
| Algorithm | SHA-256d |
| Blocks Mined | Growing — check live explorer |
| Nodes Running | 2 nodes active |
| Explorer | him-silvr.github.io/SILVR/explorer.html |
| Twitter | @SILVRprotocol |


---

## Mine SILVR on Windows (Easiest Way — WSL)

Step 1 — Enable WSL
Press Windows key on your keyboard
Type: Turn Windows features on or off
Scroll down and tick: Windows Subsystem for Linux
Click OK and restart your computer

Step 2 — Install Ubuntu
Open Microsoft Store
Search: Ubuntu
Install Ubuntu 22.04

Step 3 — Open Ubuntu terminal
Press Windows key
Type: Ubuntu
Click Open

Step 4 — Install dependencies
Paste this and press Enter:
sudo apt install gcc libssl-dev libsecp256k1-dev make git -y

Step 5 — Download SILVR
git clone https://github.com/him-SILVR/SILVR.git
cd SILVR

Step 6 — Build SILVR
make

Step 7 — Create your wallet
gcc src/wallet/wallet.c -o silvr_wallet -lssl -lcrypto -lsecp256k1 -Iinclude
./silvr_wallet

You will see your SILVR Address and Private Key.
SAVE YOUR PRIVATE KEY — write it on paper. Never share it.

Step 8 — Start mining
./silvrd YOUR_ADDRESS_HERE

Example:
./silvrd SWLswgMRtZ8hn2VHxtJ4EJX46C4fKXDWrE

You earn 47.5 SILVR every 5 minutes automatically.

---

## Mine SILVR on Linux

Step 1 — Install dependencies:
sudo apt install gcc libssl-dev libsecp256k1-dev make git -y

Step 2 — Download SILVR:
git clone https://github.com/him-SILVR/SILVR.git
cd SILVR

Step 3 — Build:
make

Step 4 — Create your wallet:
gcc src/wallet/wallet.c -o silvr_wallet -lssl -lcrypto -lsecp256k1 -Iinclude
./silvr_wallet


Step 5 — Start mining:
./silvrd YOUR_ADDRESS_HERE

---

## Mine SILVR on Windows (Alternative — MSYS2)

Step 1 — Download MSYS2 from https://www.msys2.org
Step 2 — Open MSYS2 terminal
Step 3 — Install tools:
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-make

Step 4 — Download and build:
git clone https://github.com/him-SILVR/SILVR.git
cd SILVR
make

Step 5 — Start mining:
./silvrd.exe YOUR_ADDRESS_HERE

---

## Check Your Balance

Open this link on any device — phone, laptop, anywhere:
him-silvr.github.io/SILVR/explorer.html

Type your SILVR address in the search box.

---

## What is SILVR

Custom Layer-1 blockchain built in C from scratch.
Not a fork. Not a copy.
No ICO. No presale. No VC.

42,000,000 maximum supply forever.
SHA-256d Proof of Work — same as Bitcoin.
47.5 SILVR reward per block.
5 minute block time.

---

## Roadmap

- Genesis block mined March 16 2026 — DONE
- Code public on GitHub — DONE
- Single node running — DONE
- Wallet generator — DONE
- Block explorer live — DONE
- Real transaction signing — NEXT
- Second node connected — NEXT
- True mainnet 3 nodes — COMING

---

## Community

Twitter: https://twitter.com/SILVRprotocol
Telegram: SILVR Official
GitHub: https://github.com/him-SILVR/SILVR

---

## License

MIT — Open source forever
