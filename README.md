# 💊 Automatyczny Podajnik Leków (ESP32-S3)

Inteligentny, automatyczny dyspenser leków sterowany mikrokontrolerem **ESP32-S3**, wyposażony w bezprzewodowy panel konfiguracyjny **WebUI (Wi-Fi SoftAP)**, zegar czasu rzeczywistego **RTC DS1302**, ultradźwiękowy czujnik obecności kubeczka **AJ-SR04M** oraz 8-komorowy bęben rewolwerowy napędzany silnikiem krokowym **28BYJ-48 ze sterownikiem ULN2003**.

Projekt zawiera kompletną bazę sprzętową: kod źródłowy, dokumentację techniczną oraz gotowe modele 3D do druku obudowy i mechaniki.

---

## 🌟 Najważniejsze Funkcjonalności

1. **8-komorowy mechanizm rewolwerowy:**
   * Niezależna, precyzyjna kalibracja liczby kroków silnika dla każdego z 8 slotów.
   * Ustawianie punktu bazowego (zerowanie pozycji bębna) z poziomu WebUI.
   * Ręczne wymuszenie obrotu przez przycisk fizyczny (GPIO19) lub panel WWW.
2. **Ultradźwiękowa detekcja obecności kubeczka (AJ-SR04M):**
   * Pomiar odległości w czasie rzeczywistym z programowym filtrem eliminującym zakłócenia.
   * Dowolnie konfigurowalny przedział odległości kubka w WebUI (od 0 do 10000 cm).
   * Dedykowana sygnalizacja diodami LED:
     * 🟢 **Dioda Zielona (GPIO1):** Kubeczek wykryty.
     * 🔴 **Dioda Czerwona (GPIO2):** Brak kubeczka / stan czuwania.
3. **Harmonogram i Bezpieczeństwo Pacjenta:**
   * Do 8 zdefiniowanych dawek w ciągu doby wg zegara czasu rzeczywistego DS1302 z baterią podtrzymującą CR2032.
   * Automatyczna synchronizacja zegara z przeglądarką telefonu/komputera przy wejściu do panelu.
   * **Alarm 5-minutowy:** powtarzający się sygnał dźwiękowy z buzzera (GPIO8), jeśli pacjent nie odbierze leku w ciągu 5 minut od wydania.
   * Szybki przycisk wyciszenia dźwięków (MUTE) w WebUI.
4. **Wbudowany Panel WebUI (Dark Mode):**
   * Autonomiczna sieć Wi-Fi bez potrzeby routera: **`Podajnik Leków`** (hasło: `leki1234`).
   * Strona konfiguracyjna pod adresem: **`http://192.168.4.1`**.
   * Trwały zapis wszystkich ustawień w pamięci nieulotnej Flash (EEPROM).

---

## 🔌 Schemat Podłączeń (Pinout)

| Komponent | Pin Modułu | Pin ESP32-S3 | Typ Pinu | Funkcja |
| :--- | :--- | :--- | :--- | :--- |
| **LED Zielona** | Anoda (+) | **GPIO 1** | Digital Output | Kubeczek na miejscu |
| **LED Czerwona** | Anoda (+) | **GPIO 2** | Digital Output | Brak kubeczka / czuwanie |
| **RTC DS1302** | SCLK / CLK | **GPIO 5** | Digital Output | Zegar magistrali RTC |
| **RTC DS1302** | DAT / IO | **GPIO 6** | Bi-directional | Linia danych RTC |
| **RTC DS1302** | RST / CE | **GPIO 7** | Digital Output | Reset / Chip Enable |
| **Buzzer** | Sygnał (+) | **GPIO 8** | Digital Output | Sygnały dźwiękowe i alarmy |
| **ULN2003** | IN1 | **GPIO 10** | Digital Output | Silnik krokowy faza A |
| **ULN2003** | IN2 | **GPIO 11** | Digital Output | Silnik krokowy faza B |
| **ULN2003** | IN3 | **GPIO 12** | Digital Output | Silnik krokowy faza C |
| **ULN2003** | IN4 | **GPIO 13** | Digital Output | Silnik krokowy faza D |
| **AJ-SR04M** | TRIG | **GPIO 15** | Digital Output | Impuls wyzwalający pomiar (30 µs) |
| **AJ-SR04M** | ECHO | **GPIO 16** | Digital Input | Odpowiedź ultradźwiękowa |
| **Przycisk fizyczny** | Styk | **GPIO 19** | Digital Input | Ręczny obrót (do GND, INPUT_PULLUP) |
| **Zasilanie** | VCC / VIN | **5V / 3.3V** | Zasilanie | Zasilanie silnika (5V) i logiki |
| **Masa** | GND | **GND** | Masa | Wspólna masa układu |

---

## 📂 Struktura Plików Projektu

```text
PROJECT ESP/
├── .gitignore                   # Konfiguracja wykluczeń gita (.pio, pliki tymczasowe)
├── README.md                    # Niniejsza dokumentacja główna
├── DOKUMENTACJA.txt             # Kompletna specyfikacja techniczna (wersja tekstowa)
├── podajnik.lekow.zip           # Archiwum projektu
│
├── program/                     # Kod oprogramowania układowego (Firmware)
│   ├── program.ino              # Główny kod programu (Arduino / C++ dla ESP32-S3)
│   └── platformio.ini           # Plik konfiguracyjny środowiska PlatformIO
│
└── modele stl/                  # Komplet modeli 3D do druku obudowy i mechaniki
    ├── Bęben rewolwerowy.stl     # 8-komorowy bęben na leki
    ├── baza.stl                 # Podstawa konstrukcji podajnika
    ├── baza na sensor.stl       # Dedykowane mocowanie czujnika AJ-SR04M
    ├── dystans na silnik.stl    # Dystans montażowy silnika 28BYJ-48
    ├── dach przód.stl           # Przednia część zadaszenia
    ├── dach tył.stl             # Tylna pokrywa zadaszenia
    ├── maska.stl                # Maska czołowa urządzenia
    ├── kąt lewo.stl             # Element konstrukcyjny lewego narożnika
    └── kąt prawo.stl            # Element konstrukcyjny prawego narożnika
```

---

## 🚀 Jak Wgrać Oprogramowanie (Krok po Kroku)

### 1. Wymagania
Wymagane jest środowisko Python oraz pakiet **PlatformIO**:
```bash
pip install platformio
```

### 2. Kompilacja i wgranie na płytkę
W terminalu przejdź do folderu `program/`:
```bash
cd program
python -m platformio run --target upload
```

### 3. Monitorowanie portu szeregowego
Aby sprawdzić logi uruchomieniowe i bieżący stan:
```bash
python -m platformio device monitor -b 115200
```

> **Wskazówka (Bootloader):** Jeśli wgrywanie zatrzyma się na wykrywaniu portu COM:
> 1. Przytrzymaj przycisk **BOOT** na płytce ESP32-S3.
> 2. Kliknij krótko przycisk **RST / RESET**.
> 3. Puść przycisk **BOOT** i ponów polecenie `upload`.

---

## 📱 Pierwsze Uruchomienie i Panel WebUI

1. Włącz zasilanie podajnika.
2. Na telefonie lub laptopie połącz się z siecią Wi-Fi:
   * **SSID:** `Podajnik Leków`
   * **Hasło:** `leki1234`
3. Otwórz przeglądarkę internetową i wejdź pod adres:
   * **`http://192.168.4.1`**
4. Po wejściu na stronę:
   * Zegar RTC automatycznie zsynchronizuje się z czasem Twojego urządzenia.
   * W sekcji **Harmonogram** ustaw godziny dozowania i przypisz komory.
   * W sekcji **Kalibracja** dostosuj zakres odległości podstawionego kubeczka.
