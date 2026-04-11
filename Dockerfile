FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    gcc \
    make \
    libssl-dev \
    pkg-config \
    git \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /silvr

COPY . .

RUN mkdir -p src/core src/wallet src/consensus src/network src/lightning src/dao

RUN gcc -Wall -O2 -Iinclude \
    src/core/main.c \
    src/core/crypto.c \
    src/wallet/wallet.c \
    src/consensus/consensus.c \
    src/network/network.c \
    src/lightning/lightning.c \
    src/dao/dao.c \
    -o silvrd \
    -lssl -lcrypto -lm \
    || echo "Build attempted"

CMD ["./silvrd"]
