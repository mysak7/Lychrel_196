#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <omp.h> // OpenMP

// --- KONFIGURACE ---
const std::string STATE_FILE = "lychrel_brutal.state";
const int SAVE_INTERVAL_SEC = 600; 
const int LOG_INTERVAL_SEC = 2;    

class AvxLychrel {
    std::vector<uint8_t> primary;
    std::vector<uint8_t> buffer;

public:
    AvxLychrel() {
        primary.reserve(2000000); 
        buffer.reserve(2000000);
    }

    void set_from_string(const std::string& s) {
        primary.clear();
        primary.reserve(s.length() + 1000000);
        for (auto it = s.rbegin(); it != s.rend(); ++it) primary.push_back(*it - '0');
        buffer.reserve(primary.capacity());
    }

    void set_from_int(unsigned long long n) {
        primary.clear();
        if (n == 0) primary.push_back(0);
        while (n > 0) {
            primary.push_back(n % 10);
            n /= 10;
        }
    }

    void step() {
        size_t n_size = primary.size();
        long long n = (long long)n_size; // OpenMP vyžaduje signed typ

        if (buffer.capacity() < n_size + 1) buffer.reserve(n_size + 1000000);
        buffer.resize(n_size);

        const uint8_t* p_in = primary.data();
        uint8_t* p_out = buffer.data();

        // 1. PARALELNÍ SČÍTÁNÍ (OpenMP)
        #pragma omp parallel for schedule(static)
        for (long long i = 0; i < n; ++i) {
            p_out[i] = p_in[i] + p_in[n - 1 - i];
        }

        // 2. NORMALIZACE (Carry) - Sekvenční
        uint8_t carry = 0;
        for (long long i = 0; i < n; ++i) {
            uint8_t val = p_out[i] + carry;
            if (val >= 10) {
                p_out[i] = val - 10;
                carry = 1;
            } else {
                p_out[i] = val;
                carry = 0;
            }
        }

        if (carry) buffer.push_back(1);
        primary.swap(buffer);
    }

    std::string to_string() const {
        std::string s;
        s.reserve(primary.size());
        for (auto it = primary.rbegin(); it != primary.rend(); ++it) s += std::to_string(*it);
        return s;
    }

    size_t size() const { return primary.size(); }
};

void save_state(unsigned long iter, const AvxLychrel& num) {
    std::string tmp = STATE_FILE + ".tmp";
    std::ofstream out(tmp);
    if (out.is_open()) {
        out << iter << "\n" << num.to_string();
        out.close();
        std::remove(STATE_FILE.c_str());
        std::rename(tmp.c_str(), STATE_FILE.c_str());
        std::cout << "--- ULOZENO ---" << std::endl;
    }
}

int main() {
    std::cout << "--- START ---" << std::endl;
    #ifdef _OPENMP
        std::cout << "OpenMP: AKTIVNI" << std::endl;
        std::cout << "Jader: " << omp_get_num_procs() << std::endl;
    #else
        std::cerr << "!!! VAROVANI: OpenMP NENI AKTIVNI !!!" << std::endl;
    #endif
    std::cout << "-------------" << std::endl;

    AvxLychrel num;
    unsigned long iter = 0;

    // Načtení stavu
    std::ifstream in(STATE_FILE);
    if (in.is_open()) {
        std::string s;
        if (in >> iter >> s) {
            num.set_from_string(s);
            std::cout << "Pokracuji: Iterace " << iter << " (" << num.size() << " cifer)" << std::endl;
        } else {
            std::cerr << "CHYBA: Nepodarilo se nacist data ze souboru " << STATE_FILE << std::endl;
            num.set_from_int(196);
            std::cout << "Start: 196 (z duvodu chyby cteni)" << std::endl;
        }
    } else {
        std::cout << "Soubor " << STATE_FILE << " nenalezen nebo nejde otevrit." << std::endl;
        num.set_from_int(196);
        std::cout << "Start: 196" << std::endl;
    }

    auto last_log = std::chrono::steady_clock::now();
    auto last_save = last_log;
    size_t last_len = num.size();

    // Hlavní smyčka
    while (true) {
        num.step();
        iter++;

        // Logování a ukládání (check každých 4000 iterací)
        if (iter % 4000 == 0) { 
            auto now = std::chrono::steady_clock::now();
            
            if (std::chrono::duration_cast<std::chrono::seconds>(now - last_log).count() >= LOG_INTERVAL_SEC) {
                double secs = std::chrono::duration<double>(now - last_log).count();
                size_t len = num.size();
                double speed = (len - last_len) / secs;
                
                std::cout << "Iter: " << iter << " | Len: " << len 
                          << " | Rychlost: " << std::fixed << std::setprecision(0) << speed << " cif/s" 
                          << std::endl;
                
                last_log = now;
                last_len = len;
            }

            if (std::chrono::duration_cast<std::chrono::seconds>(now - last_save).count() > SAVE_INTERVAL_SEC) {
                save_state(iter, num);
                last_save = now;
            }
        }
    }
    return 0;
}
