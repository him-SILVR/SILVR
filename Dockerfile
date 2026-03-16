FROM ubuntu:22.04

RUN apt-get update -qq && \
    apt-get install -y \
    gcc \
    libsecp256k1-dev \
    libssl-dev \
    build-essential \
    --no-install-recommends && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY . .

RUN gcc -o silvrd \
    src/core/main.c \
    src/core/crypto.c \
    src/wallet/wallet.c \
    src/consensus/consensus.c \
    -Iinclude \
    -lsecp256k1 \
    -lssl \
    -lcrypto \
    -lm

EXPOSE 8633

CMD ["./silvrd"]
