# SILVR Protocol — v3.0

> **The People's Chain** | Chain ID: 2026 | Ticker: SILVR | SHA-256d PoW

---

## What's New in v3.0

SILVR v3.0 is a complete cryptographic overhaul from the ground up.

| Feature | v2.4 | v3.0 |
|---|---|---|
| Transaction signing | Fake hash | Real ECDSA secp256k1 |
| Address derivation | SHA256d(privkey) | RIPEMD160(SHA256(pubkey)) |
| Balance model | Account balances | True UTXO set |
| Double-spend protection | None | Full UTXO validation |
| P2P block sync | Empty handler | Full GETBLOCKS/INV/GETDATA/BLOCK |
| Merkle root | Not computed | Computed and validated in every block |
| File writes | Direct overwrite | Atomic fsync + rename |
| Peer protection | None | Rate limiting + ban score system |
| Quantum readiness | None | Versioned sig_version field, PQ migration path designed |

---

## Chain Parameters

| Parameter | Value |
|---|---|
| Chain ID | 2026 |
| Ticker | SILVR |
| Max Supply | 42,000,000 SILVR |
| Block Reward | 50 SILVR (47.5 miner + 2.5 treasury) |
| Block Time | 5 minutes |
| Halving Interval | Every 420,000 blocks |
| PoW Algorithm | SHA-256d |
| Port | 8633 |
| Genesis Date | March 16, 2026 |
| Genesis Message | 2026/01/01 Silver: The People's Chain |

---

## Source Files

| File | Purpose |
|---|---|
| `crypto_abstraction.h` | All cryptography — ECDSA signing, address derivation, key generation, PQ migration path |
| `silvr_utxo.h` | True UTXO set — double-spend prevention, atomic persistence, coinbase builder |
| `silvr_p2p.h` | P2P networking — full block sync, handshake with nonce challenge, rate limiting |
| `silvrd_v3.c` | Main node — mining loop, chain persistence, peer connection |
| `silvr_migrate.c` | Migration tool — imports v2.4 balance into v3 UTXO set |

---

## Build Instructions

### Requirements
- Windows MSYS2 MinGW64
- gcc 15.2.0
- libsecp256k1 (built from source)
- OpenSSL 3.x

### Install Dependencies
```bash
pacman -Syu
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-openssl mingw-w64-x86_64-autotools git
git clone https://github.com/bitcoin-core/secp256k1.git
cd secp256k1 && ./autogen.sh && ./configure --prefix=/mingw64 --enable-module-recovery && make && make install
```

### Build Node
```bash
gcc -std=c99 -O2 silvrd_v3.c -I/mingw64/include -L/mingw64/lib -lsecp256k1 -lssl -lcrypto -lws2_32 -o silvrd_v3.exe
```

### Run Node
```bash
./silvrd_v3.exe
```

### Connect to Peer
```bash
./silvrd_v3.exe <peer_ip>
```

---

## Cryptographic Architecture

### Address Derivation (Bitcoin-standard)
```
privkey (32 bytes, random)
    → secp256k1 pubkey (33 bytes compressed)
    → SHA256(pubkey)
    → RIPEMD160(SHA256(pubkey))  [20 bytes = pubkey hash]
    → version(0x3F) || pubkey_hash || SHA256d(version||pubkey_hash)[0..3]
    → Base58Check encode
    → Address starting with 'S'
```

### Quantum Migration Path
- Current: `sig_version=0` ECDSA compact, `sig_version=1` ECDSA DER
- Future: `sig_version=2` CRYSTALS-Dilithium2 (NIST PQC standard)
- Activation: height-based soft fork at block 1,000,000
- No chain split — old ECDSA outputs remain spendable

---

## Founder Allocation

- Address: `SWLswgMRtZ8hn2VHxtJ4EJX46C4fKXDWrE`
- Allocation: 2,100,000 SILVR (5% of max supply)
- Lock period: 4 years from genesis
- Mined blocks as of v3 launch: 46,913

---

## Links

- Twitter: [@SILVRprotocol](https://twitter.com/SILVRprotocol)
- GitHub: [him-SILVR/SILVR](https://github.com/him-SILVR/SILVR)

---

*Built from scratch in C by one person. No VC funding. No pre-mine beyond founder allocation. The People's Chain.*
