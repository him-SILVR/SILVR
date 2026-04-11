FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    gcc \
    make \
    libssl-dev \
    libsecp256k1-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /silvr

COPY . .

RUN make

CMD ["./silvrd"]
