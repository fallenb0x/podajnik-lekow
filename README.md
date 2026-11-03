# 💊 Automatyczny Podajnik Leków (ESP32-S3)

Inteligentny, automatyczny dyspenser leków sterowany mikrokontrolerem **ESP32-S3** z wbudowanym serwerem WebUI, zegarem czasu rzeczywistego RTC DS1302, detekcją kubeczka przez fotorezystor oraz 8-komorowym bębnem napędzanym silnikiem krokowym.

---

## 📸 Funkcjonalności projektu

1. **8-komorowy bęben rewolwerowy:**
   * Niezależna kalibracja kroków silnika krokowego (28BYJ-48 + ULN2003) dla każdego z 8 slotów.
   * Możliwość precyzyjnego ustawienia punktu zero (pozycja bazowa).
   * Ręczny obrót z poziomu panelu WebUI lub fizycznego przycisku (GPIO19).
2. **Zaawansowana detekcja obecności kubeczka:**
   * Fotorezystor KY-018 na wejściu analogowym GPIO4.
   * Wygaszanie diod podczas pomiaru (eliminacja zakłóceń własnego światła).
   * Filtr synchroniczny sieci 50 Hz eliminujący migotanie sztucznego oświetlenia.
   * 24 niezależne progi czułości dla każdej godziny doby (automatyczne dopasowanie do oświetlenia dziennego i nocnego).
   * Wizualna sygnalizacja stanu diodami LED:
     * **Zielona LED (GPIO1):** Kubeczek jest na miejscu.
     * **Czerwona LED (GPIO2):** Brak kubeczka.
3. **Harmonogram i alarmy:**
   * Codzienny, automatyczny obrót o ustalonej godzinie (zegar czasu rzeczywistego DS1302).
   * Przypomnienie po 5 minutach: jeśli po wydaniu leku kubeczek nie zostanie odebrany, głośniczek (GPIO8) odtwarza w kółko głośną melodię, dopóki kubek nie zostanie zabrany.
   * Opcja wyciszenia głośniczka jednym kliknięciem.
4. **Wbudowany Panel WebUI (Dark Mode):**
   * Płytka rozgłasza własną sieć Wi-Fi: **`Podajnik Leków`** (hasło: `leki1234`).
   * Panel dostępny w przeglądarce pod adresem: **`http://192.168.4.1`**.
   * Podgląd stanu czujnika na żywo, synchronizacja czasu z telefonem, kalibracja slotów i progów.
   * Wszystkie ustawienia zapisywane są trwale w pamięci Flash (EEPROM).

---

## 🔌 Schemat podłączenia (Pinout)

| Komponent | Pin modułu | Pin ESP32-S3 | Opis |
|---|---|---|---|
| **Zielona LED** | Anoda (+) | **GPIO1** | Sygnalizacja obecności kubeczka |
| **Czerwona LED** | Anoda (+) | **GPIO2** | Sygnalizacja braku kubeczka |
| **Fotorezystor KY-018** | Sygnał (S) | **GPIO4** | Odczyt natężenia światła (ADC) |
| **DS1302 RTC** | CLK (SCLK) | **GPIO5** | Zegar magistrali RTC |
| **DS1302 RTC** | DAT (I/O) | **GPIO6** | Linia danych RTC |
| **DS1302 RTC** | RST (CE) | **GPIO7** | Reset / Chip Enable RTC |
| **Głośnik / Buzzer** | Sygnał (+) | **GPIO8** | Sygnalizacja dźwiękowa i melodie |
| **ULN2003 Driver** | IN1 | **GPIO10** | Silnik krokowy faza A |
| **ULN2003 Driver** | IN2 | **GPIO11** | Silnik krokowy faza B |
| **ULN2003 Driver** | IN3 | **GPIO12** | Silnik krokowy faza C |
| **ULN2003 Driver** | IN4 | **GPIO13** | Silnik krokowy faza D |
| **Przycisk fizyczny** | Styk | **GPIO19** | Ręczne wymuszenie obrotu (do GND) |
| **Zasilanie modułów** | VCC | **3V3 / 5V** | Zasilanie logiki i silnika |
| **Masa** | GND | **GND** | Wspólna masa układu |

---

## 📂 Struktura repozytorium

```text
PROJECT ESP/
├── .gitignore               # Pliki ignorowane przez Git (np. .pio)
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

## 🚀 Jak wgrać program na ESP32

### Opcja 1: Przez PlatformIO (Python)
W katalogu `program/`:
```bash
python -m platformio run --target upload
```

### Opcja 2: Przez Arduino IDE
1. Zainstaluj w menedżerze bibliotek: **`Rtc by Makuna`**.
2. Wybierz płytkę: **`ESP32-S3 Dev Module`** oraz właściwy port szeregowy COM.
3. Kliknij **Wgraj** (`Ctrl + U`).
