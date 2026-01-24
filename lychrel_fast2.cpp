#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <fstream>
#include <chrono>
#include <iomanip>

// Konfigurace
const std::string STATE_FILE = "lychrel_fast.state";
const int SAVE_INTERVAL_SEC = 30; 
const int LOG_INTERVAL_SEC = 1;

// Třída pro práci s velkými čísly v desítkové soustavě (Base 10)
// Ukládá cifry v "Little Endian" (index 0 je jednotky, index 1 desítky...)
struct BigDec {
    std::vector<uint8_t> digits;

    BigDec() {}

    // Inicializace z integeru
    BigDec(unsigned long long n) {
        if (n == 0) digits.push_back(0);
        while (n > 0) {
            digits.push_back(n % 10);
            n /= 10;
        }
    }

    // Inicializace ze stringu (pro načítání ze souboru)
    BigDec(const std::string& s) {
        digits.reserve(s.length());
        // String je "Big Endian" (první znak je nejvyšší řád), my chceme opak
        for (auto it = s.rbegin(); it != s.rend(); ++it) {
            digits.push_back(*it - '0');
        }
    }

    // Klíčová optimalizace: PŘIČTENÍ REVERZU
    // Místo vytváření kopie, otáčení a sčítání, uděláme vše v jednom průchodu.
    // A = A + Reverse(A)
    void add_reverse_in_place() {
        size_t n = digits.size();
        std::vector<uint8_t> new_digits;
        new_digits.reserve(n + 1); // Pravděpodobně porosteme o 1 cifru

        uint8_t carry = 0;
        for (size_t i = 0; i < n; ++i) {
            // Sčítáme cifru zepředu (i) s cifrou zezadu (n - 1 - i)
            uint8_t val = digits[i] + digits[n - 1 - i] + carry;
            
            if (val >= 10) {
                new_digits.push_back(val - 10);
                carry = 1;
            } else {
                new_digits.push_back(val);
                carry = 0;
            }
        }

        if (carry) {
            new_digits.push_back(carry);
        }

        // Prohodíme buffery (velmi rychlé, žádné kopírování)
        digits.swap(new_digits);
    }

    // Převod na string (pro ukládání)
    std::string to_string() const {
        std::string s;
        s.reserve(digits.size());
        for (auto it = digits.rbegin(); it != digits.rend(); ++it) {
            s += std::to_string(*it);
        }
        return s;
    }

    size_t size() const {
        return digits.size();
    }
};

void save_state(unsigned long iter, const BigDec& num) {
    std::ofstream out(STATE_FILE + ".tmp");
    if (out.is_open()) {
        out << iter << "\n" << num.to_string();
        out.close();
        // Atomický rename (bezpečnější)
        std::remove(STATE_FILE.c_str());
        std::rename((STATE_FILE + ".tmp").c_str(), STATE_FILE.c_str());
        std::cout << "--- Checkpoint ulozen (Iterace: " << iter << ") ---" << std::endl;
    }
}

int main() {
    BigDec num;
    unsigned long iter = 0;

    // Načtení stavu
    std::ifstream in(STATE_FILE);
    if (in.good()) {
        std::string num_str;
        in >> iter >> num_str;
        num = BigDec(num_str);
        std::cout << "Navazuji na iteraci: " << iter << " (Delka: " << num.size() << ")" << std::endl;
    } else {
        num = BigDec(196);
        std::cout << "Zacinam od nuly (196)" << std::endl;
    }

    auto last_save = std::chrono::steady_clock::now();
    auto last_log = std::chrono::steady_clock::now();
    size_t last_len = num.size();

    try {
        while (true) {
            // JÁDRO PROGRAMU - teď extrémně rychlé
            num.add_reverse_in_place();
            iter++;

            // Logování (stejné jako předtím)
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::seconds>(now - last_log).count() >= LOG_INTERVAL_SEC) {
                double elapsed = std::chrono::duration<double>(now - last_log).count();
                size_t current_len = num.size();
                double speed = (current_len - last_len) / elapsed;
                
                std::cout << "Iter: " << iter 
                          << ", Cifer: " << current_len 
                          << ", +Cif/s: " << std::fixed << std::setprecision(2) << speed
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
    } catch (const std::exception& e) {
        std::cerr << "Chyba: " << e.what() << std::endl;
        save_state(iter, num);
    }

    return 0;
}
