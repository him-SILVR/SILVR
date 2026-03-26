CC = gcc
CFLAGS = -Wall -Wextra -O2 -Iinclude
LDFLAGS = -lssl -lcrypto -lm -lsecp256k1

SRCS = src/core/main.c \
       src/core/crypto.c \
       src/wallet/wallet.c \
       src/consensus/consensus.c \
       src/network/network.c \
       src/lightning/lightning.c \
       src/dao/dao.c

TARGET = silvrd

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS) $(LDFLAGS)
	@echo "SILVR node built. Run: ./silvrd"

clean:
	rm -f $(TARGET)
