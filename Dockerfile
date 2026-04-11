FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    gcc \
    make \
    libssl-dev \
    libsecp256k1-dev \
    pkg-config \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /silvr

COPY . .

RUN gcc -O2 -Iinclude \
    -Wno-unused-result \
    -Wno-unused-variable \
    -Wno-unused-parameter \
    src/core/main.c \
    src/core/crypto.c \
    src/wallet/wallet.c \
    src/consensus/consensus.c \
    src/network/network.c \
    src/lightning/lightning.c \
    src/dao/dao.c \
    -o silvrd \
    -lssl -lcrypto -lm -lsecp256k1

EXPOSE 8633

CMD ["./silvrd"]
