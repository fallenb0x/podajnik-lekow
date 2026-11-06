# 💊 Automatyczny Podajnik Leków (ESP32-S3)

Inteligentny, automatyczny dyspenser leków sterowany mikrokontrolerem **ESP32-S3** z wbudowanym panelem WebUI (SoftAP), zegarem czasu rzeczywistego RTC DS1302, detekcją kubeczka ultradźwiękowym czujnikiem **AJ-SR04M** oraz 8-komorowym bębnem napędzanym silnikiem krokowym **28BYJ-48 (ULN2003)**.

---

## 📸 Główne Funkcjonalności

1. **8-komorowy bęben rewolwerowy:**
   * Niezależna kalibracja kroków silnika krokowego (28BYJ-48 + ULN2003) dla każdego z 8 slotów.
   * Możliwość precyzyjnego ustawienia punktu zero (pozycja bazowa).
   * Ręczny obrót z poziomu panelu WebUI lub fizycznego przycisku (GPIO19).
2. **Ultradźwiękowa detekcja obecności kubeczka (AJ-SR04M):**
   * Pomiar odległości w czasie rzeczywistym (TRIG: GPIO15, ECHO: GPIO16).
   * Programowa filtracja zakłóceń i eliminacja fałszywych odczytów.
   * Dowolnie konfigurowalny zakres odległości w WebUI (np. min. 3 cm, max. 15 cm – obsługa do 10000 cm).
   * Wizualna sygnalizacja stanu diodami LED:
     * **Zielona LED (GPIO1):** Kubeczek jest na miejscu (dozowanie odblokowane).
     * **Czerwona LED (GPIO2):** Brak kubeczka / stan czuwania (blokada wysypania leków).
3. **Harmonogram i Bezpieczeństwo:**
   * Dozowanie leków wg precyzyjnego zegara czasu rzeczywistego RTC DS1302 z bateryjnym podtrzymaniem.
   * Auto-synchronizacja godziny z przeglądarką telefonu/komputera przy wejściu do panelu.
   * **Alarm przypominający:** jeśli po podaniu leku kubek nie zostanie odebrany w ciągu 5 minut, głośniczek (GPIO8) emituje powtarzający się sygnał alarmowy.
   * Opcja wyciszenia głośniczka jednym kliknięciem (MUTE) w WebUI.
4. **Wbudowany Panel WebUI (Dark Mode):**
   * Własny punkt dostępowy Wi-Fi: **`Podajnik Leków`** (hasło: `leki1234`).
   * Panel dostępny w przeglądarce pod adresem: **`http://192.168.4.1`**.
   * Podgląd odległości na żywo, konfiguracja harmonogramu, kalibracja slotów i prędkości silnika.
   * Wszystkie parametry zapisywane są trwale w pamięci EEPROM / Flash.

---

## 🔌 Schemat Podłączenia (Pinout Matrix)

| Komponent | Pin Modułu | Pin ESP32-S3 | Typ Pinu | Opis i Funkcja |
| :--- | :--- | :--- | :--- | :--- |
| **LED Zielona** | Anoda (+) | **GPIO 1** | Digital Output | Sygnalizacja obecności kubka |
| **LED Czerwona** | Anoda (+) | **GPIO 2** | Digital Output | Sygnalizacja braku kubka / czuwanie |
| **DS1302 RTC** | SCLK / CLK | **GPIO 5** | Digital Output | Zegar magistrali RTC |
| **DS1302 RTC** | IO / DAT | **GPIO 6** | Bi-directional | Linia danych RTC |
| **DS1302 RTC** | CE / RST | **GPIO 7** | Digital Output | Chip Enable / Reset magistrali |
| **Buzzer** | Sygnał (+) | **GPIO 8** | Digital Output | Sygnalizacja dźwiękowa i alarmy |
| **ULN2003** | IN1 | **GPIO 10** | Digital Output | Silnik krokowy faza A |
| **ULN2003** | IN2 | **GPIO 11** | Digital Output | Silnik krokowy faza B |
| **ULN2003** | IN3 | **GPIO 12** | Digital Output | Silnik krokowy faza C |
| **ULN2003** | IN4 | **GPIO 13** | Digital Output | Silnik krokowy faza D |
| **AJ-SR04M** | TRIG | **GPIO 15** | Digital Output | Impuls wyzwalający pomiar (P15) |
| **AJ-SR04M** | ECHO | **GPIO 16** | Digital Input | Odpowiedź ultradźwiękowa (P16) |
| **Przycisk** | Styk | **GPIO 19** | Digital Input | Ręczny obrót bębna (do GND, INPUT_PULLUP) |
| **Zasilanie** | VCC / VIN | **5V / 3.3V** | Power | Silnik 5V; logika 3.3V/5V |
| **Masa** | GND | **GND** | Ground | Wspólna masa układu |

---

## 📂 Struktura Repozytorium

```text
PROJECT ESP/
├── .gitignore               # Pliki ignorowane przez Git
├── README.md                # Dokumentacja projektu
├── program/                 # Kod źródłowy mikrokontrolera
│   ├── program.ino          # Główny kod programu (Arduino / C++)
│   └── platformio.ini       # Konfiguracja środowiska PlatformIO
└── modele stl/              # Modele 3D do druku obudowy i mechanizmu
    ├── Bęben rewolwerowy.stl
    ├── baza.stl
    ├── czujnik i ledy.stl
    ├── dach przód.stl
    ├── dach tył.stl
    └── maska.stl
```

---

## 🚀 Jak wgrać program na ESP32-S3

### Wymagania:
Zainstalowany Python oraz PlatformIO:
```bash
pip install platformio
```

### Kompilacja i wgranie:
W katalogu `program/`:
```bash
python -m platformio run --target upload
```

### Tryb programowania (Bootloader):
Jeśli port COM nie odpowiada podczas wgrywania:
1. Przytrzymaj przycisk **BOOT** na płytce ESP32-S3.
2. Wciśnij krótko przycisk **RESET (RST)**.
3. Puść przycisk **BOOT**.
4. Ponów polecenie `python -m platformio run --target upload`.
