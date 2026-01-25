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
        // Read into a temp buffer then reverse, or read directly?
        // Reading directly into vector is fastest, then we fill digits.
        // Actually, we need to reverse the order. File is Big Endian.
        
        std::vector<char> buffer(size);
        if (!file.read(buffer.data(), size)) return false;

        // Convert ASCII to int and reverse
        // #pragma omp parallel for // Optional if we had OpenMP
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
            // Nothing to do for this thread
            ctx->status[id] = {false, true}; // Propagates? Doesn't matter, won't receive carry beyond capacity
            // Wait for phase 2
            ctx->barrier->wait();
            // Wait for phase 3
            ctx->barrier->wait();
            continue;
        }

        // --- Phase 1: Add and Local Carry Propagation ---
        const uint8_t* in = ctx->current->digits.data();
        uint8_t* out = ctx->next->digits.data();

        // Check if we need to extend 'next' vector. Main thread handles resize.
        // Assuming 'next' is already sized to N. (Extensions handled later)

        bool gen = false;
        bool prop = true; // Assume propagates until proven otherwise

        // We process the block.
        // sum[i] = in[i] + in[N-1-i]
        // We can do a single pass to compute sum and local carries.
        // To determine 'prop', we need to see if a carry entering at 'start' would reach 'end'.
        
        uint8_t local_carry = 0;
        
        for (size_t i = start; i < end; ++i) {
            uint8_t sum = in[i] + in[N - 1 - i] + local_carry;
            if (sum >= 10) {
                out[i] = sum - 10;
                local_carry = 1;
            } else {
                out[i] = sum;
                local_carry = 0;
            }
        }
        
        // 'local_carry' is now the carry generated by this block purely from addition.
        gen = (local_carry == 1);

        // Now compute 'prop'.
        // Propagates if all digits in the output block are 9s?
        // If we add 1 to the first digit, does it ripple to the end?
        // This is true if out[i] == 9 for all i in [start, end).
        // Wait, if out[k] != 9, the ripple stops there.
        // Optimization: Check this during the loop?
        // It's cheaper to check separately or integrate.
        // Let's integrate.
        // Actually, the previous loop calculated the "base" result (assuming carry_in=0).
        // We need to know if an incoming carry would propagate through.
        
        // Re-checking propagation condition:
        prop = true;
        for (size_t i = start; i < end; ++i) {
            if (out[i] != 9) {
                prop = false;
                break;
            }
        }

        ctx->status[id] = {gen, prop};

        ctx->barrier->wait(); // Wait for all to finish Phase 1

        // --- Phase 2: Sequential Carry Scan (Main Thread or Single Thread) ---
        // We let thread 0 do it, others wait.
        // Or we could do a parallel scan, but for ~16-32 threads, sequential is instant.
        
        if (id == 0) {
            ctx->carry_in[0] = 0;
            for (size_t i = 0; i < num_blocks; ++i) {
                // Carry into next block depends on current block
                // CarryOut_i = Gen_i || (Prop_i && CarryIn_i)
                bool c_out = ctx->status[i].generated || (ctx->status[i].propagates && (ctx->carry_in[i] == 1));
                if (i + 1 < ctx->carry_in.size()) {
                    ctx->carry_in[i+1] = c_out ? 1 : 0;
                } else if (c_out) {
                    // Overflow from the very last block!
                    // This means the number grew in size.
                    // We handle this by resizing 'next' later.
                    // For now, we just note it.
                    // But wait, 'next' size is N. If we have a carry out of N, 
                    // we need to append 1. Thread 0 can't resize vector while others are using data() pointers?
                    // Actually, data pointers are valid if we reserved enough.
                    // But we'll handle growth after the barrier.
                }
            }
        }

        ctx->barrier->wait(); // Wait for Phase 2

        // --- Phase 3: Apply Carry In ---
        if (ctx->carry_in[id] == 1) {
            // Add 1 to the start of our block and ripple
            uint8_t c = 1;
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
            // We don't need to worry about c propagating out of 'end', 
            // because if it did, 'prop' would have been true, 
            // and the next block would already have carry_in=1.
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
        // Try to find latest dump
        // Simplification: Require filename argument or assume standard
        std::cerr << "Usage: " << argv[0] << " <dump_file>" << std::endl;
        return 1;
    }

    // Parse iteration count from filename "dump.196.ITER"
    size_t start_num = 0;
    size_t iter_count = 0;
    size_t last_dot = filename.find_last_of('.');
    size_t first_dot = filename.find_first_of('.');
    if (last_dot != std::string::npos && first_dot != std::string::npos && last_dot != first_dot) {
         try {
             iter_count = std::stoull(filename.substr(last_dot + 1));
             start_num = std::stoull(filename.substr(first_dot + 1, last_dot - first_dot - 1));
         } catch (...) {
             std::cerr << "Could not parse iteration count from filename. Using 0." << std::endl;
         }
    }

    std::cout << "Loading " << filename << " (Iter: " << iter_count << ")..." << std::endl;

    BigInt num1, num2;
    if (!num1.load(filename)) {
        std::cerr << "Failed to load file." << std::endl;
        return 1;
    }

    num1.digits.reserve(num1.digits.size() + 10000); // Pre-allocate some growth
    num2.digits.reserve(num1.digits.size() + 10000);

    unsigned int num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0) num_threads = 4;
    std::cout << "Using " << num_threads << " threads." << std::endl;

    // Thread Context Setup
    ThreadContext ctx;
    ctx.num_threads = num_threads;
    ctx.status.resize(num_threads);
    ctx.carry_in.resize(num_threads + 1); // +1 for the overflow carry
    Barrier barrier(num_threads + 1); // +1 for main thread
    ctx.barrier = &barrier;

    std::vector<std::thread> threads;
    for (unsigned int i = 0; i < num_threads; ++i) {
        threads.emplace_back(thread_worker, i, &ctx);
    }

    auto start_time = std::chrono::steady_clock::now();
    auto last_save = start_time;

    std::cout << "Starting calculation..." << std::endl;

    while (true) {
        // Prepare for iteration
        // num1 is current, num2 is next
        ctx.current = &num1;
        ctx.next = &num2;
        
        // Ensure destination has size
        size_t cur_size = num1.digits.size();
        if (num2.digits.size() < cur_size) {
            num2.digits.resize(cur_size);
        }
        
        // Release threads for Phase 1
        barrier.wait();

        // Threads doing Phase 1...
        
        // Wait for Phase 1 completion
        barrier.wait();
        
        // Threads doing Phase 2 (Sequential) wait for thread 0...
        
        // Wait for Phase 2 completion
        barrier.wait();
        
        // Threads doing Phase 3...
        
        // Wait for Phase 3 completion
        barrier.wait();

        // Check for final carry overflow (stored in carry_in[num_threads] during Phase 2? No, wait)
        // In Phase 2, thread 0 calculated carry_in array.
        // We need to check ctx.carry_in[num_threads].
        if (ctx.carry_in[num_threads]) {
            num2.digits.push_back(1);
        }

        // Swap buffers
        std::swap(num1.digits, num2.digits); // Efficient swap
        // num2 (old num1) might have wrong size now, but we resize it at start of loop
        // Actually, we should resize num2 to num1.size() to be safe or just let it grow.
        // Better to resize at top.

        iter_count++;

        // Periodic Save/Log
        if (iter_count % 1000 == 0) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_save).count();
            if (elapsed >= SAVE_INTERVAL_SECONDS) {
                std::string new_filename = "dump.196." + std::to_string(iter_count);
                std::cout << "Saving to " << new_filename << " (Digits: " << num1.digits.size() << ")..." << std::endl;
                if (num1.save(new_filename)) {
                    // Optionally delete old file
                    std::remove(filename.c_str());
                    filename = new_filename;
                    last_save = now;
                } else {
                    std::cerr << "Save failed!" << std::endl;
                }
            }
            
            // Speed Log
            if (iter_count % 10000 == 0) {
                double total_time = std::chrono::duration<double>(now - start_time).count();
                std::cout << "Iter: " << iter_count << " | Len: " << num1.digits.size() 
                          << " | Avg Speed: " << (total_time > 0 ? (iter_count / total_time) : 0) << " iter/s" << std::endl;
            }
        }
    }

    // Cleanup (unreachable infinite loop)
    ctx.terminate = true;
    barrier.wait();
    for (auto& t : threads) t.join();

    return 0;
}
