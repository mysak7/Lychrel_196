#include <iostream>
#include <gmpxx.h>
#include <string>
#include <algorithm>
#include <fstream>
#include <chrono>
#include <iomanip>

// Konfigurace
const std::string STATE_FILE = "lychrel.state";
const int SAVE_INTERVAL_SEC = 300; // Ukládat každých 5 minut
const int LOG_INTERVAL_SEC = 1;    // Vypisovat info každou sekundu

void save_state(unsigned long iter, const mpz_class& num) {
    std::ofstream out(STATE_FILE + ".tmp");
    out << iter << "\n" << num.get_str();
    out.close();
    rename((STATE_FILE + ".tmp").c_str(), STATE_FILE.c_str());
    std::cout << "--- Checkpoint ulozen (Iterace: " << iter << ") ---" << std::endl;
}

int main() {
    mpz_class num;
    unsigned long iter = 0;

    // Načtení stavu
    std::ifstream in(STATE_FILE);
    if (in.good()) {
        std::string num_str;
        in >> iter >> num_str;
        num = num_str; // GMP C++ wrapper umí stringy přímo
        std::cout << "Navazuji na iteraci: " << iter << std::endl;
    } else {
        num = 196;
        std::cout << "Zacinam od nuly (196)" << std::endl;
    }

    auto last_save = std::chrono::steady_clock::now();
    auto last_log = std::chrono::steady_clock::now();
    mpz_class reversed_num;
    std::string s;
    size_t last_len = 0;

    // Prvotní délka
    if (num != 0) {
        last_len = num.get_str().length();
    }

    try {
        while (true) {
            // 1. Získání stringu (nejdražší operace - bottleneck)
            s = num.get_str();
            
            // Logování rychlosti a stavu
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::seconds>(now - last_log).count() >= LOG_INTERVAL_SEC) {
                double elapsed = std::chrono::duration<double>(now - last_log).count();
                double speed = (s.length() - last_len) / elapsed;
                
                std::cout << "Iterace: " << iter
                          << ", Cifer: " << s.length()
                          << ", Rychlost: " << std::fixed << std::setprecision(2) << speed << " cif/s"
                          << std::endl;

                last_log = now;
                last_len = s.length();
            }

            // 2. Otočení stringu (in-place)
            std::reverse(s.begin(), s.end());
            
            // 3. Převod zpět na číslo
            reversed_num.set_str(s, 10); // Explicitně base 10 kvůli vedoucím nulám
            
            // 4. Sečíst (využívá GMP optimalizace)
            num += reversed_num;
            iter++;

            // Ukládání
            if (std::chrono::duration_cast<std::chrono::seconds>(now - last_save).count() > SAVE_INTERVAL_SEC) {
                save_state(iter, num);
                last_save = now;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Chyba (exception): " << e.what() << std::endl;
        save_state(iter, num);
    } catch (...) {
        std::cerr << "Neznama chyba (unknown exception)" << std::endl;
        save_state(iter, num);
    }

    return 0;
}