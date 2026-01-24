#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <cstdio> // pro remove/rename

// --- KONFIGURACE ---
const std::string STATE_FILE = "lychrel.state3";
const int SAVE_INTERVAL_SEC = 600; // 5 minut
const int LOG_INTERVAL_SEC = 1;    // 1 sekunda
// Kolik paměti navíc alokovat, aby se nemuselo resizeovat každou iteraci
// 1024 cifer navíc znamená, že realokace proběhne jen jednou za 1024 iterací.
const size_t ALLOC_CHUNK = 4096; 

struct BigDec {
    // Používáme dva buffery. 'primary' drží aktuální číslo.
    // 'aux' slouží pro zápis výsledku sčítání.
    // Po výpočtu je prohodíme (swap), což je O(1).
    std::vector<uint8_t> primary;
    std::vector<uint8_t> aux;

    BigDec() {
        primary.reserve(ALLOC_CHUNK);
        aux.reserve(ALLOC_CHUNK);
    }

    // Inicializace z čísla
    void set(unsigned long long n) {
        primary.clear();
        if (n == 0) primary.push_back(0);
        while (n > 0) {
            primary.push_back(n % 10);
            n /= 10;
        }
    }

    // Inicializace ze stringu (load ze souboru)
    void set(const std::string& s) {
        primary.clear();
        primary.reserve(s.length() + ALLOC_CHUNK);
        // String je Big-Endian, my chceme Little-Endian
        for (auto it = s.rbegin(); it != s.rend(); ++it) {
            primary.push_back(*it - '0');
        }
        // Připravíme i aux buffer, aby měl stejnou kapacitu
        aux.reserve(primary.capacity());
    }

    // --- JÁDRO ALGORITMU ---
    void step() {
        size_t n = primary.size();
        
        // 1. Příprava bufferu
        // Pokud aux nemá dost kapacity, zvětšíme ho rovnou o velký kus,
        // abychom nemuseli alokovat příštích X iterací.
        if (aux.capacity() < n + 1) {
            aux.reserve(n + ALLOC_CHUNK);
        }
        
        // Nastavíme velikost aux na n (carry dořešíme na konci)
        // resize() na vektoru 'uint8_t' je levný, nemění data, jen pointer konce.
        aux.resize(n);

        // Získáme raw pointery pro maximální rychlost (obchází range-checky vectoru)
        const uint8_t* p_in = primary.data();
        uint8_t* p_out = aux.data();

        // 2. FÁZE SČÍTÁNÍ (Vectorizable Loop)
        // Tady sčítáme číslo zepředu a zezadu.
        // Kompilátor (O3) toto dokáže zvectorizovat (AVX/SSE),
        // protože zde není závislost na předchozím prvku (carry).
        for (size_t i = 0; i < n; ++i) {
            p_out[i] = p_in[i] + p_in[n - 1 - i];
        }

        // 3. FÁZE NORMALIZACE (Carry Propagation)
        // Řešíme přenosy přes desítku. Toto musí jít sekvenčně.
        uint8_t carry = 0;
        for (size_t i = 0; i < n; ++i) {
            uint8_t val = p_out[i] + carry;
            
            // Optimalizace: Podmíněné odčítání je rychlejší než dělení (val % 10)
            if (val >= 10) {
                p_out[i] = val - 10;
                carry = 1;
            } else {
                p_out[i] = val;
                carry = 0;
            }
        }

        // Pokud zbyl přenos na konci, přidáme novou cifru
        if (carry) {
            aux.push_back(1);
        }

        // 4. PROHOZENÍ BUFFERŮ
        // Pointer swap - super rychlé
        primary.swap(aux);
    }

    std::string to_string() const {
        std::string s;
        s.reserve(primary.size());
        for (auto it = primary.rbegin(); it != primary.rend(); ++it) {
            s += std::to_string(*it);
        }
        return s;
    }

    size_t size() const {
        return primary.size();
    }
};

void save_state(unsigned long iter, const BigDec& num) {
    std::string temp_filename = STATE_FILE + ".tmp";
    std::ofstream out(temp_filename);
    if (out.is_open()) {
        out << iter << "\n" << num.to_string();
        out.close();
        
        std::remove(STATE_FILE.c_str());
        std::rename(temp_filename.c_str(), STATE_FILE.c_str());
        
        std::cout << "--- ULOZENO (Iterace: " << iter << ", Cifer: " << num.size() << ") ---" << std::endl;
    }
}

int main() {
    // Zrychlení C++ IO streamů
    std::ios_base::sync_with_stdio(false);
    
    BigDec num;
    unsigned long iter = 0;

    // Načtení stavu
    std::ifstream in(STATE_FILE);
    if (in.good()) {
        std::string num_str;
        in >> iter >> num_str;
        num.set(num_str);
        std::cout << "RESUME: Iterace " << iter << " (" << num.size() << " cifer)" << std::endl;
    } else {
        num.set(196);
        std::cout << "START: 196" << std::endl;
    }

    auto last_save = std::chrono::steady_clock::now();
    auto last_log = std::chrono::steady_clock::now();
    size_t last_len = num.size();

    try {
        while (true) {
            // --- CRITICAL PATH ---
            num.step();
            iter++;
            // ---------------------

            // Logika pro výpis a ukládání (jen občas, aby nebrzdila výpočet)
            // Zkontrolujeme čas jen každých 1000 iterací, abychom nevolali 'clock' příliš často
            if (iter % 1000 == 0) {
                auto now = std::chrono::steady_clock::now();
                
                // Logování
                if (std::chrono::duration_cast<std::chrono::seconds>(now - last_log).count() >= LOG_INTERVAL_SEC) {
                    double elapsed = std::chrono::duration<double>(now - last_log).count();
                    size_t current_len = num.size();
                    double speed = (current_len - last_len) / elapsed;
                    
                    std::cout << "Iter: " << iter 
                              << " | Len: " << current_len 
                              << " | Rychlost: " << std::fixed << std::setprecision(1) << speed << " cif/s"
                              << std::endl;

                    last_log = now;
                    last_len = current_len;
                }

                // Ukládání
                if (std::chrono::duration_cast<std::chrono::seconds>(now - last_save).count() > SAVE_INTERVAL_SEC) {
                    save_state(iter, num);
                    last_save = now;
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "CRITICAL ERROR: " << e.what() << std::endl;
        save_state(iter, num);
    }

    return 0;
}
