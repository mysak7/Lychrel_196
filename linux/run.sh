#!/bin/bash
# Example run script

if [ ! -f "p196_mpi" ]; then
    echo "Compiling..."
    make
fi

# Check for dump files
LATEST_DUMP=$(ls -v dump.196.* 2>/dev/null | tail -n 1)

if [ -z "$LATEST_DUMP" ]; then
    if [ -f "../dump.196.*" ]; then
        echo "Copying dump files from parent directory..."
        cp ../dump.196.* .
        LATEST_DUMP=$(ls -v dump.196.* | tail -n 1)
    fi
fi

if [ -z "$LATEST_DUMP" ]; then
    echo "No dump file found. Creating seed..."
    ./make_seed
    LATEST_DUMP="dump.196.0"
fi

echo "Running with 4 processes on $LATEST_DUMP"
mpirun -np 4 ./p196_mpi -i $LATEST_DUMP -d 0 -m 0 -M 0 -D 1000000
