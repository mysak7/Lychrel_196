#!/bin/bash
# Run script for C++ Lychrel solver

# Compile if needed
if [ ! -f "lychrel_new" ] && [ ! -f "lychrel_new.exe" ]; then
    echo "Compiling..."
    if [ -f "compile.sh" ]; then
        chmod +x compile.sh
        ./compile.sh
    else
        g++ -O3 -pthread -std=c++17 -o lychrel_new lychrel_new.cpp
    fi
fi

# Determine executable
EXE="./lychrel_new"
if [ -f "lychrel_new.exe" ]; then
    EXE="./lychrel_new.exe"
fi

# Check for dump files
LATEST_DUMP=$(ls -v dump.196.* 2>/dev/null | tail -n 1)

# If no local dump, check ../linux/
if [ -z "$LATEST_DUMP" ]; then
    if [ -d "../linux" ]; then
        LATEST_DUMP_REMOTE=$(ls -v ../linux/dump.196.* 2>/dev/null | tail -n 1)
        if [ ! -z "$LATEST_DUMP_REMOTE" ]; then
            echo "Copying latest dump from ../linux/..."
            cp "$LATEST_DUMP_REMOTE" .
            LATEST_DUMP=$(basename "$LATEST_DUMP_REMOTE")
        fi
    fi
fi

if [ -z "$LATEST_DUMP" ]; then
    echo "No dump file found. Creating seed..."
    # Create default seed "196" manually
    # Note: dump file format is Big Endian ASCII digits.
    echo -n "196" > dump.196.0
    LATEST_DUMP="dump.196.0"
fi

echo "Running $EXE on $LATEST_DUMP"
$EXE $LATEST_DUMP
