#!/bin/bash
echo "Building SILVR node..."
gcc -Wall -o silvrd src/core/main.c -lm
echo "Build complete"
echo "Starting SILVR node..."
./silvrd
