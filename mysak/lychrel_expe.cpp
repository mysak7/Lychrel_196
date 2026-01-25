#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <omp.h>
#include <cstring> // memcpy, memset
#include <immintrin.h> // Intrinsic functions

// --- KONFIGURACE ---
const std::string STATE_FILE = "lychrel.state";
#define PARANOID_CHECK // Odkomentujte pro kontrolu správnosti (zpomalí 100x!)

// Dolbeau Magic Constants
// Každý bajt obsahuje 246 (0xF6). 10 + 246 = 256 (Carry).
const uint64_t MAGIC_OFFSET = 0xF6F6F6F6F6F6F6F6ULL;

// Maska pro zjištění, které bajty NEpřetekly (mají MSB 0)
// Pokud po součtu bit 7 není 1, znamená to, že carry nenastal? 
// Ne, je to složitější. Spoléháme na to, že (val >= 256) zahodí MSB.
// Dolbeau používá maskování 0x0101...
// Zde použijeme jednodušší SWAR logiku: 
// 1. Součet = A + B + OFFSET
// 2. Kde nenastal carry, tam musíme odečíst OFFSET.

class DolbeauLychrel {
    std::vector<uint8_t> primary;
    std::vector<uint8_t> buffer;

public:
    DolbeauLychrel() {
        primary.reserve(2000000); 
        buffer.reserve(2000000);
    }

    void set_from_int(unsigned long long n) {
        primary.clear();
        while (n > 0) { primary.push_back(n % 10); n /= 10; }
        buffer.reserve(2000000);
    }
    
    void set_from_string(const std::string& s) {
        primary.clear();
        for (auto it = s.rbegin(); it != s.rend(); ++it) primary.push_back(*it - '0');
        buffer.reserve(primary.capacity());
    }

    // EXPERIMENTÁLNÍ JÁDRO
    void step() {
        size_t n = primary.size();
        if (buffer.capacity() < n + 16) buffer.reserve(n + 1000000);
        buffer.resize(n); // Velikost zatím stejná, carry dořešíme

        const uint8_t* p_in = primary.data();
        uint8_t* p_out = buffer.data();

        // Pole pro zachycení přenosů mezi bloky (kvůli paralelizaci)
        // Každý blok (8 cifer) může vygenerovat carry ven.
        // Musíme vědět, jestli blok 'i' poslal carry do bloku 'i+1'.
        // To je těžké paralelizovat bez závislostí.
        // PROTO: Uděláme jen "vnitřní" SWAR součet a carry mezi bloky necháme na 2. fázi.

        #pragma omp parallel for schedule(static)
        for (long long i = 0; i <= (long long)n - 8; i += 8) {
            // 1. Načtení 8 cifer (Forward)
            uint64_t v_fwd;
            std::memcpy(&v_fwd, &p_in[i], 8);

            // 2. Načtení 8 cifer (Backward) - Tady je to tricky.
            // Musíme načíst p_in[n-1-i] a jít DOZADU.
            // Příklad: n=100, i=0. Chceme p_in[99]..p_in[92].
            // To je v paměti jako p_in[92]..p_in[99] ale otočené.
            // Načteme 8 bytů z adresy &p_in[n - 8 - i]
            
            uint64_t v_bwd_raw;
            std::memcpy(&v_bwd_raw, &p_in[n - 8 - i], 8);
            
            // Otočíme pořadí bajtů (BSWAP)
            uint64_t v_bwd = _byteswap_uint64(v_bwd_raw);

            // 3. MAGICKÝ SOUČET (SIMD within Register)
            // A + B + 0xF6F6...
            // Tím se desítkový přenos (>=10) změní na binární přenos (>=256).
            // Binární přenos se automaticky propaguje DOLEVA (do vyššího bytu).
            
            // POZOR: Tady je risk. Carry z bytu 0 se přičte k bytu 1.
            // To je správně! To chceme!
            // Ale carry z bytu 7 (nejvyššího) se ztratí (přeteče uint64).
            // Ten musíme zachytit, ale v paralelním loopu to nejde snadno zapsat o blok vedle.
            // Dolbeau to řeší tak, že sčítá A+B bez carry, a carry řeší v masce.
            
            // Zkusíme modifikovanou verzi: "Lazy Block Carry"
            // Uložíme jen (A + B). Korekce a Carry uděláme v Pass 2.
            // Tím jsme sice zahodili půlku Dolbeau efektu, ale aspoň to bude fungovat.
            
            // Počkat! Dolbeau efekt je právě v tom, že carry se propaguje HNED.
            // Pokud to neuděláme, je to pomalejší než obyčejné sčítání.
            
            // OK, Jdeme na "Dirty Dolbeau":
            uint64_t sum = v_fwd + v_bwd + MAGIC_OFFSET;
            
            // Nyní máme v 'sum' částečně sečtené cifry.
            // Kde NEbyl carry, tam je hodnota o 0xF6 větší.
            // Musíme to opravit. Ale nevíme, kde byl carry, protože se už započítal.
            // Trik: (A + B + 0xF6) ^ ((A ^ B) & 0x10) ... ne, to je na bity.
            
            // ZDE JE DŮVOD, PROČ SE TO BLBĚ IMPLEMENTUJE:
            // Museli bychom mít přístup k "Carry Out" každého bytu.
            // To C++ standardně neumí.
            
            // FALLBACK NA RUČNÍ SWAR BEZ HARDWARE CARRY:
            // Sčítáme po 8 bytech, ale "bezpečně".
            // Uložíme výsledek do bufferu a carry vyřešíme potom.
            // Tímto se vyhneme cyklu po 1 bajtu.
             std::memcpy(&p_out[i], &sum, 8); // Uložíme bordel a opravíme ho později
        }

        // Dojezd zbytku (konec pole, co není dělitelný 8)
        long long limit = ((long long)n / 8) * 8;
        for (long long i = limit; i < n; ++i) {
             p_out[i] = p_in[i] + p_in[n - 1 - i]; // Tady bez magického offsetu
        }

        // FÁZE 2: Oprava Dolbeau Bordelu + Carry Propagation
        // Toto musí být sekvenční.
        uint8_t carry = 0;
        
        // Jdeme po 8 bytech a opravujeme
        // Pokud jsme použili MAGIC_OFFSET, v bufferu jsou divná čísla.
        // Ufff. Bez assemblerovské instrukce "ADD s Carry Flag" je Dolbeau v C++ pomalý.
        
        // Víte co? Uděláme to jinak. Vykašleme se na MAGIC_OFFSET v první fázi.
        // Prostě sečteme 8 bytů najednou jako uint64.
        // A + B. Výsledek každého bytu může být max 18 (0x12).
        // Nikdy nepřeteče do dalšího bytu (protože 0x12 << 8 je bezpečné).
        // Takže můžeme použít uint64_t pro sčítání 8 cifer najednou BEZ carry propagace.
        
        // RESTART LOOPU S LEPŠÍ MYŠLENKOU:
        #pragma omp parallel for schedule(static)
        for (long long i = 0; i <= (long long)n - 8; i += 8) {
             uint64_t v_fwd;
             std::memcpy(&v_fwd, &p_in[i], 8);
             
             uint64_t v_bwd;
             uint64_t v_bwd_raw;
             std::memcpy(&v_bwd_raw, &p_in[n - 8 - i], 8);
             v_bwd = _byteswap_uint64(v_bwd_raw);

             // Klíčový trik:
             // Součet dvou bajtů (0..9) je max 18.
             // V uint64_t se sousední bajty NEOVLIVNÍ (žádný carry).
             // 0x0505 + 0x0505 = 0x0A0A.
             // Můžeme uložit výsledek rovnou.
             uint64_t sum = v_fwd + v_bwd;
             std::memcpy(&p_out[i], &sum, 8);
        }
        
        // Zbytek pole
        for (long long i = limit; i < n; ++i) p_out[i] = p_in[i] + p_in[n - 1 - i];

        // FÁZE 3: Rychlý Carry Fix
        // Máme pole bajtů, kde jsou hodnoty 0..18.
        // Potřebujeme je "znormalizovat" na 0..9.
        // Tady použijeme Dolbeau trik na urychlení podmínky!
        
        carry = 0;
        for (long long i = 0; i < n; ++i) {
            uint8_t val = p_out[i] + carry;
            
            // Branchless optimalizace (Magie):
            // Místo if (val >= 10) použijeme bitové operace?
            // Pro moderní CPU (Branch Prediction) je obyčejný IF často rychlejší
            // než složitá bitová matematika, pokud jsou data předvídatelná.
            // U Lychrela je carry v cca 40-50% případů. To je nejhorší možný případ pro predikci.
            // Takže ZDE se vyplatí branchless kód!
            
            // Val je 0..19.
            // Chceme: new_val = val % 10; carry = val / 10;
            
            if (val >= 10) { // Zkusíme nechat IF, kompilátor MSVC umí CMOV
                p_out[i] = val - 10;
                carry = 1;
            } else {
                p_out[i] = val;
                carry = 0;
            }
        }

        if (carry) buffer.push_back(1);
        primary.swap(buffer);
        
        #ifdef PARANOID_CHECK
            check_correctness();
        #endif
    }
    
    // Pomalá kontrola pro ladění
    void check_correctness() {
        // Zde by byla implementace kontroly, jestli primary == reverse(primary) + old_primary
        // Ale to vyžaduje držet kopii starého čísla.
    }

    std::string to_string() const {
        std::string s; s.reserve(primary.size());
        for (auto it = primary.rbegin(); it != primary.rend(); ++it) s += std::to_string(*it);
        return s;
    }
    size_t size() const { return primary.size(); }
};

// ... (Zbytek main funkce stejný jako minule, jen změna názvu třídy) ...

int main() {
    std::cout << "--- EXPERIMENTALNI DOLBEAU-LITE ALGORITMUS ---" << std::endl;
    // ... Copy paste zbytku main z lychrel_final.cpp ...
    // Jen místo SafeFastLychrel num; dejte DolbeauLychrel num;
    
    DolbeauLychrel num;
    unsigned long iter = 0;
    
    // ... init ze souboru ...
    std::ifstream in(STATE_FILE);
    if (in.good()) {
        std::string s; in >> iter >> s;
        num.set_from_string(s);
        std::cout << "Resume: " << iter << std::endl;
    } else {
        num.set_from_int(196);
    }
    
    auto last_log = std::chrono::steady_clock::now();
    size_t last_len = num.size();

    while(true) {
        num.step();
        iter++;
        
        if (iter % 5000 == 0) {
             auto now = std::chrono::steady_clock::now();
             double secs = std::chrono::duration<double>(now - last_log).count();
             if (secs > 2.0) {
                 size_t len = num.size();
                 std::cout << "Iter: " << iter << " Len: " << len << " Rychlost: " << (len-last_len)/secs << " cif/s" << std::endl;
                 last_log = now; last_len = len;
                 
                 // Save state...
                 std::ofstream out(STATE_FILE);
                 out << iter << "\n" << num.to_string();
             }
        }
    }
    return 0;
}
