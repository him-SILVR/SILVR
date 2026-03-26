CC = gcc
CFLAGS = -Wall -Wextra -O2 -Iinclude
LDFLAGS = -lssl -lcrypto -lm

SRCS = src/core/main.c \
       src/wallet/wallet.c

TARGET = silvrd

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS) $(LDFLAGS)
	@echo "SILVR node built. Run: ./silvrd"

clean:
	rm -f $(TARGET)
