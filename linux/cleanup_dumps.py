
import os
import glob

# --- CONFIGURATION ---
# Set to False to actually delete files. 
# Set to True to just print what would happen.
DRY_RUN = True  
# ---------------------

def get_iteration(filename):
    """Extracts the iteration number from dump.196.<iteration>"""
    try:
        return int(filename.split('.')[-1])
    except (IndexError, ValueError):
        return -1

def cleanup():
    # Find all dump files
    files = glob.glob("dump.196.*")
    
    # Data structure: size -> list of (iteration, filename)
    files_by_size = {}
    to_delete = []

    print(f"Scanning {len(files)} files...")

    for f in files:
        try:
            size = os.path.getsize(f)
            
            # Rule 1: Delete 0 byte files immediately
            if size == 0:
                to_delete.append(f)
                continue

            # Group non-zero files by size
            if size not in files_by_size:
                files_by_size[size] = []
            
            iteration = get_iteration(f)
            files_by_size[size].append((iteration, f))
            
        except OSError as e:
            print(f"Error accessing {f}: {e}")

    # Process grouped files
    for size, file_list in files_by_size.items():
        # Rule 2: If size is a multiple of 10MB (10,000,000), keep ALL versions
        # We assume exact bytes based on your 'ls' output (e.g., 20000000)
        if size % 10_000_000 == 0:
            print(f"[KEEP ALL] Size {size:,} bytes (Multiple of 10MB) - Keeping {len(file_list)} files.")
            continue

        # Rule 3: For other sizes, keep only the LAST version (highest iteration)
        # Sort by iteration (ascending)
        file_list.sort(key=lambda x: x[0])
        
        # The last element is the one to keep
        keep = file_list[-1]
        
        # All others in this list are to be deleted
        remove_list = file_list[:-1]
        
        if remove_list:
            print(f"[CLEANUP]  Size {size:,} bytes - Keeping {keep[1]}, Deleting {len(remove_list)} older versions.")
            for _, fname in remove_list:
                to_delete.append(fname)

    # Perform Deletion
    print("\n--- DELETION SUMMARY ---")
    if not to_delete:
        print("No files to delete.")
    
    for fname in to_delete:
        if DRY_RUN:
            print(f"[DRY RUN] Would delete: {fname}")
        else:
            try:
                os.remove(fname)
                print(f"[DELETED] {fname}")
            except OSError as e:
                print(f"[ERROR] Could not delete {fname}: {e}")

    if DRY_RUN:
        print("\n!!! DRY RUN MODE ACTIVE !!!")
        print("No files were actually deleted.")
        print("Edit the script and set DRY_RUN = False to execute.")

if __name__ == "__main__":
    cleanup()
