#!/bin/bash

# Configuration
SOURCE_DIR="$HOME/Lychrel_196/linux/"
DEST_DIR="/mnt/koofr/"

# Colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo -e "${GREEN}Starting backup from ${SOURCE_DIR} to ${DEST_DIR}...${NC}"

# Check if source exists
if [ ! -d "$SOURCE_DIR" ]; then
    echo -e "${RED}Error: Source directory $SOURCE_DIR does not exist.${NC}"
    exit 1
fi

# Check if destination is mounted (simple check if directory exists and is a mountpoint)
if ! mountpoint -q "$DEST_DIR"; then
    echo -e "${RED}Warning: $DEST_DIR is not a mountpoint. Is Koofr mounted?${NC}"
    read -p "Continue anyway? (y/n) " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        exit 1
    fi
fi

# RSYNC COMMAND EXPLAINED:
# -r: recursive
# -v: verbose
# --size-only: checksums are slow on mounts; size check is faster
# --no-perms --no-owner --no-group: Essential for WebDAV mounts (prevents permission errors)
# --progress: shows progress bar
rsync -rv --size-only --no-perms --no-owner --no-group --progress "$SOURCE_DIR" "$DEST_DIR"

if [ $? -eq 0 ]; then
    echo -e "${GREEN}Backup completed successfully!${NC}"
else
    echo -e "${RED}Backup failed with errors.${NC}"
fi
