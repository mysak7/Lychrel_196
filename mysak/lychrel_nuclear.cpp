#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <omp.h> // OpenMP pro multithreading

// --- KONFIGURACE ---
const std::string STATE_FILE = "lychrel.state";
const int SAVE_INTERVAL_SEC = 300; // Ukládat každých 5 minut
const int LOG_INTERVAL_SEC = 2;    // Logovat každé 2 sekundy

class SafeFastLychrel {
    // Používáme uint8_t (1 byte = 1 cifra). 
    // Je to nejbezpečnější metoda. Base 10^9 je náchylná na chyby v zarovnání.
    std::vector<uint8_t> primary;
    std::vector<uint8_t> buffer;

public:
    SafeFastLychrel() {
        primary.reserve(2000000); 
        buffer.reserve(2000000);
    }

    // Inicializace ze stringu
    void set_from_string(const std::string& s) {
        primary.clear();
        primary.reserve(s.length() + 1000000);
        // String "123" -> Vector [3, 2, 1] (Little Endian)
        for (auto it = s.rbegin(); it != s.rend(); ++it) primary.push_back(*it - '0');
        buffer.reserve(primary.capacity());
    }

    // Inicializace z čísla
    void set_from_int(unsigned long long n) {
        primary.clear();
        if (n == 0) primary.push_back(0);
        while (n > 0) {
            primary.push_back(n % 10);
            n /= 10;
        }
    }

    // --- JÁDRO VÝPOČTU ---
    void step() {
        size_t n_size = primary.size();
        long long n = (long long)n_size; // Signed typ pro OpenMP (MSVC vyžaduje)

        // Příprava bufferu
        if (buffer.capacity() < n_size + 16) buffer.reserve(n_size + 1000000);
        buffer.resize(n_size);

        const uint8_t* p_in = primary.data();
        uint8_t* p_out = buffer.data();

        // 1. FÁZE: PARALELNÍ SČÍTÁNÍ (Hrubá síla)
        // Využije všechna jádra CPU. Paměťová propustnost je zde limit.
        #pragma omp parallel for schedule(static)
        for (long long i = 0; i < n; ++i) {
            // Sečteme cifru zepředu a cifru zezadu
            // Výsledek (max 18) uložíme. Přenosy neřešíme (řeší 2. fáze).
            p_out[i] = p_in[i] + p_in[n - 1 - i];
        }

        // 2. FÁZE: NORMALIZACE (Carry)
        // Musí běžet sekvenčně, ale je velmi rychlá (jen porovnání).
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

        // Pokud přetekl poslední řád, přidáme cifru
        if (carry) buffer.push_back(1);

        // Prohodíme buffery (vektor swap je O(1))
        primary.swap(buffer);
    }

    // Pomocná funkce pro kontrolu (Self-Test)
    bool check_value(const std::string& expected) const {
        return to_string() == expected;
    }

    std::string to_string() const {
        std::string s;
        s.reserve(primary.size());
        for (auto it = primary.rbegin(); it != primary.rend(); ++it) s += std::to_string(*it);
        return s;
    }

    size_t size() const { return primary.size(); }
};

// --- SELF TEST ---
// Spustí se před hlavním výpočtem, aby se ověřilo, že CPU/RAM počítá správně.
void run_self_test() {
    std::cout << "Probiha SELF-TEST algoritmu..." << std::endl;
    SafeFastLychrel test_num;
    
    // Test 1: 196 + 691 = 887
    test_num.set_from_int(196);
    test_num.step();
    if (!test_num.check_value("887")) {
        std::cerr << "CHYBA: Test 1 selhal! (Cekano 887)" << std::endl;
        exit(1);
    }

    // Test 2: Pretečení (Carry)
    // 56 + 65 = 121
    test_num.set_from_int(56);
    test_num.step();
    if (!test_num.check_value("121")) {
        std::cerr << "CHYBA: Test 2 selhal! (Cekano 121)" << std::endl;
        exit(1);
    }

    std::cout << "SELF-TEST OK. Algoritmus funguje spravne." << std::endl;
}

void save_state(unsigned long iter, const SafeFastLychrel& num) {
    // Zapisujeme do .tmp a pak přejmenujeme (atomická operace - bezpečné proti pádu)
    std::string tmp = STATE_FILE + ".tmp";
    std::ofstream out(tmp);
    if (out.is_open()) {
        out << iter << "\n" << num.to_string();
        out.close();
        std::remove(STATE_FILE.c_str());
        std::rename(tmp.c_str(), STATE_FILE.c_str());
        std::cout << "--- CHECKPOINT (Iter: " << iter << ") ---" << std::endl;
    }
}

int main() {
    // Rychlejší C++ I/O
    std::ios_base::sync_with_stdio(false);
    std::cout << "--- LYCHREL 196 (Safe & Fast Mode) ---" << std::endl;

    // 1. Ověření OpenMP
    #ifdef _OPENMP
        std::cout << "[INFO] OpenMP aktivni. Pocet jader: " << omp_get_num_procs() << std::endl;
    #else
        std::cerr << "[VAROVANI] OpenMP NENI aktivni! Program bude pomaly." << std::endl;
        std::cerr << "Na Windows zkompilujte s: /openmp" << std::endl;
    #endif

    // 2. Self Test
    run_self_test();

    SafeFastLychrel num;
    unsigned long iter = 0;

    // 3. Načtení stavu
    std::ifstream in(STATE_FILE);
    if (in.good()) {
        std::string s;
        in >> iter >> s;
        num.set_from_string(s);
        std::cout << "[INFO] Navazuji na iteraci: " << iter << " (Delka: " << num.size() << ")" << std::endl;
    } else {
        num.set_from_int(196);
        std::cout << "[INFO] Zacinam od nuly (196)" << std::endl;
    }

    auto start_time = std::chrono::steady_clock::now();
    auto last_log = start_time;
    auto last_save = start_time;
    size_t last_len = num.size();

    // 4. Hlavní smyčka
    while (true) {
        num.step();
        iter++;

        // Kontrola času jen občas (každých 4000 iterací), aby nebrzdila výpočet
        if (iter % 4000 == 0) { 
            auto now = std::chrono::steady_clock::now();
            
            // Logování rychlosti
            if (std::chrono::duration_cast<std::chrono::seconds>(now - last_log).count() >= LOG_INTERVAL_SEC) {
                double secs = std::chrono::duration<double>(now - last_log).count();
                size_t len = num.size();
                double speed = (len - last_len) / secs;
                
                // Výpočet času běhu
                auto total_duration = now - start_time;
                auto hours = std::chrono::duration_cast<std::chrono::hours>(total_duration);
                total_duration -= hours;
                auto minutes = std::chrono::duration_cast<std::chrono::minutes>(total_duration);
                total_duration -= minutes;
                auto seconds = std::chrono::duration_cast<std::chrono::seconds>(total_duration);

                std::cout << "Iter: " << iter << " | Len: " << len
                          << " | Rychlost: " << std::fixed << std::setprecision(0) << speed << " cif/s"
                          << " | Cas: " << hours.count() << "h " << minutes.count() << "m " << seconds.count() << "s"
                          << std::endl;
                
                last_log = now;
                last_len = len;
            }

            // Ukládání
            if (std::chrono::duration_cast<std::chrono::seconds>(now - last_save).count() > SAVE_INTERVAL_SEC) {
                save_state(iter, num);
                last_save = now;
            }
        }
    }
    return 0;
}
