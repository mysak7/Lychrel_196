#!/bin/bash
# Example run script

# Always ensure binary is up to date
echo "Checking compilation..."
if ! make; then
    echo "Compilation failed!"
    exit 1
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

# Auto-detect available processing units (vCPUs)
if command -v nproc >/dev/null; then
    DEFAULT_NP=$(nproc)
else
    DEFAULT_NP=1
fi

NP=${1:-$DEFAULT_NP}
echo "Running with $NP processes on $LATEST_DUMP"
# Use hardware threads and bind to them for better EPYC utilization
mpirun --oversubscribe --use-hwthread-cpus --bind-to hwthread -np $NP ./p196_mpi -i $LATEST_DUMP -d 0 -m 0 -M 0 -D 1000000
