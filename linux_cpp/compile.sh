#!/bin/bash
echo "Compiling lychrel_new..."
g++ -O3 -pthread -std=c++17 -o lychrel_new lychrel_new.cpp
if [ $? -eq 0 ]; then
    echo "Compilation successful."
else
    echo "Compilation failed."
    exit 1
fi
