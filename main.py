import gmpy2
import os
import time
import sys

# --- KONFIGURACE ---
FILENAME = "lychrel_196.txt"
SAVE_INTERVAL = 300  # Ukládat každých 300 sekund (5 minut)

def load_state():
    if os.path.exists(FILENAME):
        with open(FILENAME, "r") as f:
            data = f.read().strip().split(":")
            return int(data[0]), gmpy2.mpz(data[1])
    return 0, gmpy2.mpz(196)

def save_state(iteration, number):
    # Bezpečný zápis: nejprve do temp souboru, pak přejmenovat (atomic operation)
    temp_name = FILENAME + ".tmp"
    with open(temp_name, "w") as f:
        f.write(f"{iteration}:{number.digits()}")
    os.replace(temp_name, FILENAME)
    print(f"--- ULOŽENO (Iterace: {iteration}, Cifer: {len(number.digits())}) ---")

# --- HLAVNÍ SMYČKA ---
iteration, num = load_state()
last_save = time.time()
print(f"Startuji od iterace: {iteration}")

# Zpracování argumentů (cílová iterace nebo počet kroků)
target_iteration = None
if len(sys.argv) > 1:
    try:
        arg_val = int(sys.argv[1])
        # Pokud je argument větší než aktuální iterace, bereme ho jako cílovou iteraci.
        # Pokud je menší (např. 1000), bereme ho jako počet kroků, které se mají přidat.
        if arg_val > iteration:
            target_iteration = arg_val
            print(f"Cíl nastaven na iteraci: {target_iteration}")
        else:
            target_iteration = iteration + arg_val
            print(f"Bude provedeno {arg_val} iterací (cíl: {target_iteration})")
    except ValueError:
        print("Neplatný argument. Běží nekonečně.")

# Statistiky
session_start = time.time()
start_digits = -1
last_print = 0

try:
    while True:
        # Kontrola limitu
        if target_iteration is not None and iteration >= target_iteration:
            print(f"\nDosazeno cílové iterace {target_iteration}. Ukládám a končím...")
            save_state(iteration, num)
            break
        # 1. Získat číslo jako řetězec a otočit
        s_num = num.digits()
        current_digits = len(s_num)

        if start_digits == -1:
            start_digits = current_digits
            session_start = time.time()

        if time.time() - last_print >= 1.0:
            elapsed = time.time() - session_start
            speed = (current_digits - start_digits) / elapsed if elapsed > 0 else 0.0
            print(f"Iterace: {iteration}, Cifer: {current_digits}, Rychlost: {speed:.2f} cif/s")
            last_print = time.time()

        r_num = gmpy2.mpz(s_num[::-1])
        
        # 2. Kontrola palindromu (volitelné, zpomaluje, u 196 zbytečné v úvodu)
        # if num == r_num: ...
        
        # 3. Sečíst (Tady se děje magie GMP)
        num = num + r_num
        iteration += 1

        # 4. Ukládání jednou za čas
        if time.time() - last_save > SAVE_INTERVAL:
            save_state(iteration, num)
            last_save = time.time()

except KeyboardInterrupt:
    print("\nPřerušeno uživatelem. Ukládám...")
    save_state(iteration, num)
