#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "isf.h"

int main() {
    size_t full_size = 3;
    size_t start = 196;
    size_t step = 0;
    // 196 -> units=6, tens=9, hundreds=1
    char current[3] = {6, 9, 1};
    
    if (write_isf("dump.196.0.isf", full_size, start, step, current) != 0) {
        fprintf(stderr, "Failed to write dump.196.0.isf\n");
        return 1;
    }
    printf("Created dump.196.0.isf\n");
    return 0;
}
