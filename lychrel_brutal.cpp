#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <omp.h> // Vyžaduje OpenMP

// --- KONFIGURACE ---
const std::string STATE_FILE = "lychrel.state4";
const int SAVE_INTERVAL_SEC = 600; 
const int LOG_INTERVAL_SEC = 2;    

class BrutalLychrel {
    std::vector<uint8_t> primary;
    std::vector<uint8_t> buffer;

public:
    BrutalLychrel() {
        // Rezerva paměti, aby se nemuselo často alokovat
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

    // --- MAIN LOOP ---
    void step() {
        size_t n = primary.size();
        
        if (buffer.capacity() < n + 1) buffer.reserve(n + 1000000);
        buffer.resize(n);

        const uint8_t* p_in = primary.data();
        uint8_t* p_out = buffer.data();

        // PARALELNÍ ČÁST - VŽDY ZAPNUTA
        // static schedule je nejrychlejší pro tento typ operací (stejná práce pro všechny)
        #pragma omp parallel for schedule(static)
        for (size_t i = 0; i < n; ++i) {
            p_out[i] = p_in[i] + p_in[n - 1 - i];
        }

        // SEKVENČNÍ ČÁST (Carry)
        // Toto musí běžet v jednom vlákně, ale je to velmi rychlé
        uint8_t carry = 0;
        for (size_t i = 0; i < n; ++i) {
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

void save_state(unsigned long iter, const BrutalLychrel& num) {
    std::string tmp = STATE_FILE + ".tmp";
    std::ofstream out(tmp);
    out << iter << "\n" << num.to_string();
    out.close();
    std::remove(STATE_FILE.c_str());
    std::rename(tmp.c_str(), STATE_FILE.c_str());
    std::cout << "--- ULOZENO ---" << std::endl;
}

int main() {
    // Diagnostika OpenMP
    std::cout << "--- START DIAGNOSTIKY ---" << std::endl;
    #ifdef _OPENMP
        std::cout << "OpenMP: AKTIVNI (Verze: " << _OPENMP << ")" << std::endl;
        std::cout << "Pocet dostupnych jader: " << omp_get_num_procs() << std::endl;
        std::cout << "Max pocet vlaken: " << omp_get_max_threads() << std::endl;
    #else
        std::cerr << "!!! VAROVANI: OpenMP NENI AKTIVNI !!!" << std::endl;
        std::cerr << "Program bezi pouze na 1 jadre." << std::endl;
        std::cerr << "Zkompilujte s -fopenmp (Linux) nebo /openmp (Windows)" << std::endl;
    #endif
    std::cout << "-------------------------" << std::endl;

    BrutalLychrel num;
    unsigned long iter = 0;

    std::ifstream in(STATE_FILE);
    if (in.good()) {
        std::string s;
        in >> iter >> s;
        num.set_from_string(s);
        std::cout << "Pokracuji od iterace: " << iter << std::endl;
    } else {
        num.set_from_int(196);
        std::cout << "Start: 196" << std::endl;
    }

    auto start_time = std::chrono::steady_clock::now();
    auto last_log = start_time;
    auto last_save = start_time;
    size_t last_len = num.size();

    // Hlavní smyčka
    while (true) {
        num.step();
        iter++;

        if (iter % 4000 == 0) { // Check jen občas
            auto now = std::chrono::steady_clock::now();
            
            if (std::chrono::duration_cast<std::chrono::seconds>(now - last_log).count() >= LOG_INTERVAL_SEC) {
                double secs = std::chrono::duration<double>(now - last_log).count();
                size_t len = num.size();
                double speed = (len - last_len) / secs;
                
                auto total_seconds = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
                int hours = total_seconds / 3600;
                int minutes = (total_seconds % 3600) / 60;
                int seconds = total_seconds % 60;

                std::cout << "Iter: " << iter << " | Len: " << len
                          << " | Rychlost: " << std::fixed << std::setprecision(0) << speed << " cif/s"
                          << " | Cas: " << hours << "h " << minutes << "m " << seconds << "s"
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
