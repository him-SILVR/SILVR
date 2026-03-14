CC = gcc
CFLAGS = -Wall -Wextra -O2 -Iinclude
LDFLAGS = -lsecp256k1 -lssl -lcrypto -lm

SRCS = src/core/main.c \
       src/core/crypto.c \
       src/wallet/wallet.c \
       src/consensus/consensus.c

TARGET = silvrd

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS) $(LDFLAGS)
	@echo ""
	@echo "================================================"
	@echo "  SILVR node built successfully"
	@echo "  Run: ./silvrd"
	@echo "  Run with your key: ./silvrd YOUR_PRIVATE_KEY"
	@echo "================================================"

clean:
	rm -f $(TARGET)
