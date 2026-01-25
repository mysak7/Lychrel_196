#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <cstring>
#include <cmath>
#include <immintrin.h>

// Modern C++ replacement for p196_mpi
// Uses std::thread for parallelization and a Carry-Lookahead approach for addition.

// --- Barrier Implementation (C++11 compatible) ---
class Barrier {
private:
    std::mutex m_mutex;
    std::condition_variable m_cv;
    size_t m_count;
    size_t m_initial;
    size_t m_generation;

public:
    explicit Barrier(size_t count) : m_count(count), m_initial(count), m_generation(0) {}

    void wait() {
        std::unique_lock<std::mutex> lock(m_mutex);
        size_t gen = m_generation;
        if (--m_count == 0) {
            m_generation++;
            m_count = m_initial;
            m_cv.notify_all();
        } else {
            m_cv.wait(lock, [this, gen] { return gen != m_generation; });
        }
    }
};

// --- Configuration ---
const int SAVE_INTERVAL_SECONDS = 300; // Auto-save every 5 minutes

struct BlockStatus {
    bool generated; // Carry generated out of this block (internally)
    bool propagates; // Block propagates an incoming carry
};

class BigInt {
public:
    std::vector<uint8_t> digits; // Little Endian (index 0 is ones digit)
    // Using uint8_t is efficient. 0-9 values.

    BigInt() = default;

    // Load from dump file (Big Endian ASCII digits)
    bool load(const std::string& filename) {
        std::ifstream file(filename, std::ios::binary | std::ios::ate);
        if (!file.is_open()) return false;

        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);

        digits.resize(size);
        std::vector<char> buffer(size);
        if (!file.read(buffer.data(), size)) return false;

        // Convert ASCII to int and reverse
        for (size_t i = 0; i < size; ++i) {
            digits[i] = buffer[size - 1 - i] - '0';
        }
        return true;
    }

    // Save to dump file (Big Endian ASCII digits)
    bool save(const std::string& filename) const {
        std::ofstream file(filename, std::ios::binary);
        if (!file.is_open()) return false;

        // We need to write in reverse order
        size_t size = digits.size();
        std::vector<char> buffer(size);
        
        // Convert to ASCII and reverse
        for (size_t i = 0; i < size; ++i) {
            buffer[size - 1 - i] = digits[i] + '0';
        }

        return file.write(buffer.data(), size).good();
    }
};

// Global state for threads
struct ThreadContext {
    BigInt* current;
    BigInt* next;
    std::vector<BlockStatus> status;
    std::vector<uint8_t> carry_in; // Carry into each block
    Barrier* barrier;
    size_t num_threads;
    bool terminate = false;
};

// AVX2 Kernel function (Adapted from p196_mpi)
void process_block_avx2(const uint8_t* current, uint8_t* next, size_t start, size_t end, size_t full_size, bool& out_gen, bool& out_prop) {
    size_t i = start;
    uint8_t carry = 0;
    bool all_nines = true;

    // Constants
    const __m256i pb = _mm256_set1_epi8(10);
    const __m256i pz = _mm256_setzero_si256();
    // Shuffling vector for mirror (inter-lane shuffle not supported by _mm256_shuffle_epi8 directly)
    const __m256i pe = _mm256_set_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
                                       0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    const __m256i p246 = _mm256_set1_epi8(246);
    const __m256i p9 = _mm256_set1_epi8(9);

    size_t last_safe = (end >= 128) ? end - 128 : start;
    
    // Align 'i' if possible? No, 'current' allocation might not be aligned to 32 bytes.
    // We use loadu (unaligned).

    for (; i <= last_safe; i += 128) { // <= because we process i to i+128
        if (i + 128 > end) break; // Extra safety

        __m256i o1, o2, o1b, o2b, o1c, o2c, o1d, o2d;
        __m256i pc, pcb, pcc, pcd;
        __m256i r, rb, rc, rd;
        __m256i mask, maskb, maskc, maskd;
        __m256i temp, tempb, tempc, tempd;
        __m256i c1to2, c2to3, c3to4;
        __m256i pc2, pc2b, pc2c, pc2d;
        int cc;

        // Load regular
        o1  = _mm256_loadu_si256((const __m256i*)(current + i + 0));
        o1b = _mm256_loadu_si256((const __m256i*)(current + i + 32));
        o1c = _mm256_loadu_si256((const __m256i*)(current + i + 64));
        o1d = _mm256_loadu_si256((const __m256i*)(current + i + 96));

        // Load mirrored (reverse)
        // Indices: full_size - (i + 32), etc.
        o2  = _mm256_loadu_si256((const __m256i*)(current + full_size - (i + 32)));
        o2b = _mm256_loadu_si256((const __m256i*)(current + full_size - (i + 64)));
        o2c = _mm256_loadu_si256((const __m256i*)(current + full_size - (i + 96)));
        o2d = _mm256_loadu_si256((const __m256i*)(current + full_size - (i + 128)));

        // Mirror the 32-bytes loaded
        o2  = _mm256_shuffle_epi8(o2 ,pe);
        o2b = _mm256_shuffle_epi8(o2b,pe);
        o2c = _mm256_shuffle_epi8(o2c,pe);
        o2d = _mm256_shuffle_epi8(o2d,pe);
        
        o2  = _mm256_permute2x128_si256(o2,o2,0x01);
        o2b = _mm256_permute2x128_si256(o2b,o2b,0x01);
        o2c = _mm256_permute2x128_si256(o2c,o2c,0x01);
        o2d = _mm256_permute2x128_si256(o2d,o2d,0x01);

        // Incoming carry
        pc = _mm256_inserti128_si256(pz, _mm_cvtsi32_si128((int)carry), 0);
        pcb = pz; pcc = pz; pcd = pz;
        carry = 0;

        // Step 1: Add
        o1  = _mm256_add_epi8(o1 ,p246);
        o1b = _mm256_add_epi8(o1b,p246);
        o1c = _mm256_add_epi8(o1c,p246);
        o1d = _mm256_add_epi8(o1d,p246);

        r  = _mm256_add_epi64(o1 , o2 );
        rb = _mm256_add_epi64(o1b, o2b);
        rc = _mm256_add_epi64(o1c, o2c);
        rd = _mm256_add_epi64(o1d, o2d);

        r  = _mm256_add_epi64(r , pc );
        rb = _mm256_add_epi64(rb, pcb);
        rc = _mm256_add_epi64(rc, pcc);
        rd = _mm256_add_epi64(rd, pcd);

        // Carry propagation loop
        do {
             mask  = _mm256_cmpgt_epi8(pz, r);
             maskb = _mm256_cmpgt_epi8(pz, rb);
             maskc = _mm256_cmpgt_epi8(pz, rc);
             maskd = _mm256_cmpgt_epi8(pz, rd);

             r  = _mm256_sub_epi8(r , _mm256_and_si256(mask , p246));
             rb = _mm256_sub_epi8(rb, _mm256_and_si256(maskb, p246));
             rc = _mm256_sub_epi8(rc, _mm256_and_si256(maskc, p246));
             rd = _mm256_sub_epi8(rd, _mm256_and_si256(maskd, p246));

             mask  = _mm256_cmpgt_epi8(r , p9);
             maskb = _mm256_cmpgt_epi8(rb, p9);
             maskc = _mm256_cmpgt_epi8(rc, p9);
             maskd = _mm256_cmpgt_epi8(rd, p9);

             pc  = _mm256_sub_epi8(pz, mask );
             pcb = _mm256_sub_epi8(pz, maskb);
             pcc = _mm256_sub_epi8(pz, maskc);
             pcd = _mm256_sub_epi8(pz, maskd);

             temp  = _mm256_and_si256(pb, mask ); 
             tempb = _mm256_and_si256(pb, maskb);
             tempc = _mm256_and_si256(pb, maskc); 
             tempd = _mm256_and_si256(pb, maskd);

             r  = _mm256_sub_epi8(r , temp );
             rb = _mm256_sub_epi8(rb, tempb);
             rc = _mm256_sub_epi8(rc, tempc);
             rd = _mm256_sub_epi8(rd, tempd);

             c1to2 = _mm256_srli_si256(pc, 15);
             c1to2 = _mm256_permute2x128_si256(c1to2,pz,0x21);
             c2to3 = _mm256_srli_si256(pcb, 15);
             c2to3 = _mm256_permute2x128_si256(c2to3,pz,0x21);
             c3to4 = _mm256_srli_si256(pcc, 15);
             c3to4 = _mm256_permute2x128_si256(c3to4,pz,0x21);

             // Extract final carry from pcd
             // emulates non-existent 256bits VPEXTRB
             carry += _mm_extract_epi8(_mm256_extracti128_si256(pcd, 1), 15);

             pc2  = _mm256_srli_si256(pc, 15);
             pc2  = _mm256_permute2x128_si256(pc2,pz,0x02);
             pc2b = _mm256_srli_si256(pcb, 15);
             pc2b = _mm256_permute2x128_si256(pc2b,pz,0x02);
             pc2c = _mm256_srli_si256(pcc, 15);
             pc2c = _mm256_permute2x128_si256(pc2c,pz,0x02);
             pc2d = _mm256_srli_si256(pcd, 15);
             pc2d = _mm256_permute2x128_si256(pc2d,pz,0x02);

             pc  = _mm256_slli_si256(pc , 1);
             pc  = _mm256_add_epi8(pc , pc2 );
             pcb = _mm256_slli_si256(pcb, 1);
             pcb = _mm256_add_epi8(pcb, pc2b);
             pcc = _mm256_slli_si256(pcc, 1);
             pcc = _mm256_add_epi8(pcc, pc2c);
             pcd = _mm256_slli_si256(pcd, 1);
             pcd = _mm256_add_epi8(pcd, pc2d);

             pcb = _mm256_add_epi8(pcb, c1to2);
             pcc = _mm256_add_epi8(pcc, c2to3);
             pcd = _mm256_add_epi8(pcd, c3to4);

             cc = _mm256_testz_si256(pc,pc) && _mm256_testz_si256(pcb,pcb) && 
                  _mm256_testz_si256(pcc,pcc) && _mm256_testz_si256(pcd,pcd);

             // If not done, prepare for next iteration
             if (!cc) {
                 r  = _mm256_add_epi64(r ,p246);
                 rb = _mm256_add_epi64(rb,p246);
                 rc = _mm256_add_epi64(rc,p246);
                 rd = _mm256_add_epi64(rd,p246);

                 r  = _mm256_add_epi64(r , pc );
                 rb = _mm256_add_epi64(rb, pcb);
                 rc = _mm256_add_epi64(rc, pcc);
                 rd = _mm256_add_epi64(rd, pcd);
                 // And loop back to checks...
             }
        } while (!cc);

        // Store
        _mm256_storeu_si256((__m256i*)(next + i + 0), r );
        _mm256_storeu_si256((__m256i*)(next + i + 32), rb);
        _mm256_storeu_si256((__m256i*)(next + i + 64), rc);
        _mm256_storeu_si256((__m256i*)(next + i + 96), rd);

        // Update all_nines check
        // Check if any digit != 9
        // Compare with 9
        __m256i neq9 = _mm256_andnot_si256(_mm256_cmpeq_epi8(r, p9), _mm256_set1_epi8(-1));
        if (!_mm256_testz_si256(neq9, neq9)) all_nines = false;
        if (all_nines) {
             neq9 = _mm256_andnot_si256(_mm256_cmpeq_epi8(rb, p9), _mm256_set1_epi8(-1));
             if (!_mm256_testz_si256(neq9, neq9)) all_nines = false;
        }
        if (all_nines) {
             neq9 = _mm256_andnot_si256(_mm256_cmpeq_epi8(rc, p9), _mm256_set1_epi8(-1));
             if (!_mm256_testz_si256(neq9, neq9)) all_nines = false;
        }
        if (all_nines) {
             neq9 = _mm256_andnot_si256(_mm256_cmpeq_epi8(rd, p9), _mm256_set1_epi8(-1));
             if (!_mm256_testz_si256(neq9, neq9)) all_nines = false;
        }
    }

    // Scalar fallback
    for (; i < end; ++i) {
        uint8_t sum = current[i] + current[full_size - 1 - i] + carry;
        if (sum >= 10) {
            next[i] = sum - 10;
            carry = 1;
        } else {
            next[i] = sum;
            carry = 0;
        }
        if (next[i] != 9) all_nines = false;
    }

    out_gen = (carry == 1);
    out_prop = all_nines;
}

void thread_worker(int id, ThreadContext* ctx) {
    while (true) {
        ctx->barrier->wait(); // Wait for start of iteration
        if (ctx->terminate) return;

        const size_t N = ctx->current->digits.size();
        const size_t num_blocks = ctx->num_threads;
        
        // Calculate block range
        size_t block_size = (N + num_blocks - 1) / num_blocks;
        size_t start = id * block_size;
        size_t end = std::min(start + block_size, N);

        if (start >= N) {
            ctx->status[id] = {false, true}; 
            ctx->barrier->wait();
            ctx->barrier->wait();
            continue;
        }

        // --- Phase 1: Add and Local Carry Propagation (AVX2) ---
        const uint8_t* in = ctx->current->digits.data();
        uint8_t* out = ctx->next->digits.data();

        bool gen, prop;
        process_block_avx2(in, out, start, end, N, gen, prop);
        
        ctx->status[id] = {gen, prop};

        ctx->barrier->wait(); // Wait for all to finish Phase 1

        // --- Phase 2: Sequential Carry Scan (Main Thread or Single Thread) ---
        if (id == 0) {
            ctx->carry_in[0] = 0;
            for (size_t i = 0; i < num_blocks; ++i) {
                // Carry into next block depends on current block
                bool c_out = ctx->status[i].generated || (ctx->status[i].propagates && (ctx->carry_in[i] == 1));
                if (i + 1 < ctx->carry_in.size()) {
                    ctx->carry_in[i+1] = c_out ? 1 : 0;
                }
            }
        }

        ctx->barrier->wait(); // Wait for Phase 2

        // --- Phase 3: Apply Carry In ---
        if (ctx->carry_in[id] == 1) {
            // Add 1 to the start of our block and ripple
            uint8_t c = 1;
            // This ripple is likely short, scalar is fine.
            for (size_t i = start; i < end; ++i) {
                uint8_t val = out[i] + c;
                if (val >= 10) {
                    out[i] = val - 10;
                    c = 1;
                } else {
                    out[i] = val;
                    c = 0;
                    break; // Stopped propagating
                }
            }
        }
        
        ctx->barrier->wait(); // Finish iteration
    }
}

int main(int argc, char** argv) {
    // Determine input file
    std::string filename;
    if (argc > 1) {
        filename = argv[1]; // Use provided filename
    } else {
        std::cerr << "Usage: " << argv[0] << " <dump_file>" << std::endl;
        return 1;
    }

    // Parse iteration count from filename "dump.196.ITER"
    size_t iter_count = 0;
    size_t last_dot = filename.find_last_of('.');
    if (last_dot != std::string::npos) {
         try {
             iter_count = std::stoull(filename.substr(last_dot + 1));
         } catch (...) {}
    }

    std::cout << "Loading " << filename << " (Iter: " << iter_count << ")..." << std::endl;

    BigInt num1, num2;
    if (!num1.load(filename)) {
        std::cerr << "Failed to load file." << std::endl;
        return 1;
    }

    num1.digits.reserve(num1.digits.size() + 10000); 
    num2.digits.reserve(num1.digits.size() + 10000);

    unsigned int num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0) num_threads = 4;
    std::cout << "Using " << num_threads << " threads." << std::endl;

    // Thread Context Setup
    ThreadContext ctx;
    ctx.num_threads = num_threads;
    ctx.status.resize(num_threads);
    ctx.carry_in.resize(num_threads + 1); 
    Barrier barrier(num_threads + 1);
    ctx.barrier = &barrier;

    std::vector<std::thread> threads;
    for (unsigned int i = 0; i < num_threads; ++i) {
        threads.emplace_back(thread_worker, i, &ctx);
    }

    auto start_time = std::chrono::steady_clock::now();
    auto last_save = start_time;
    auto last_log = start_time;
    size_t last_log_iter = iter_count;

    std::cout << "Starting calculation..." << std::endl;

    while (true) {
        ctx.current = &num1;
        ctx.next = &num2;
        
        size_t cur_size = num1.digits.size();
        if (num2.digits.size() < cur_size) {
            num2.digits.resize(cur_size);
        }
        
        barrier.wait(); // Phase 1
        barrier.wait(); // Phase 2
        barrier.wait(); // Phase 3
        barrier.wait(); // Finish

        // Final carry
        if (ctx.carry_in[num_threads]) {
            num2.digits.push_back(1);
        }

        std::swap(num1.digits, num2.digits);
        iter_count++;

        // Periodic Save/Log
        if (iter_count % 100 == 0) { 
            auto now = std::chrono::steady_clock::now();
            
            auto elapsed_save = std::chrono::duration_cast<std::chrono::seconds>(now - last_save).count();
            if (elapsed_save >= SAVE_INTERVAL_SECONDS) {
                std::string new_filename = "dump.196." + std::to_string(iter_count);
                std::cout << "Saving to " << new_filename << " (Digits: " << num1.digits.size() << ")..." << std::endl;
                if (num1.save(new_filename)) {
                    std::remove(filename.c_str());
                    filename = new_filename;
                    last_save = now;
                } else {
                    std::cerr << "Save failed!" << std::endl;
                }
            }
            
            auto elapsed_log = std::chrono::duration_cast<std::chrono::seconds>(now - last_log).count();
            if (elapsed_log >= 2) {
                double dt = std::chrono::duration<double>(now - last_log).count();
                double speed = (iter_count - last_log_iter) / dt;
                std::cout << "Iter: " << iter_count 
                          << " | Digits: " << num1.digits.size() 
                          << " | Speed: " << std::fixed << std::setprecision(2) << speed << " iter/s" << std::endl;
                last_log = now;
                last_log_iter = iter_count;
            }
        }
    }

    ctx.terminate = true;
    barrier.wait();
    for (auto& t : threads) t.join();

    return 0;
}
