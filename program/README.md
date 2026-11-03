# Automatyczny Podajnik Leków (ESP32)

## 📌 Podłączenie pinów
* **GPIO1** : Zielona dioda LED (sygnalizuje obecność kubeczka)
* **GPIO2** : Czerwona dioda LED (brak kubeczka / blokada podawania)
* **GPIO4** : KY-018 – Fotorezystor (wyjście sygnałowe S -> wejście analogowe ADC)
* **GPIO5** : DS1302 – CLK (zegar magistrali)
* **GPIO6** : DS1302 – DAT (linia danych IO)
* **GPIO7** : DS1302 – RST (linia Reset / CE)
* **GPIO10**: ULN2003 – IN1 (silnik krokowy 28BYJ-48)
* **GPIO11**: ULN2003 – IN2
* **GPIO12**: ULN2003 – IN3
* **GPIO13**: ULN2003 – IN4
* **GPIO19**: Przycisk fizyczny (podłączony do masy GND; używa wewnętrznego `INPUT_PULLUP`)
* **GPIO18**: Opcjonalny buzzer / głośniczek piezoelektryczny
* **3V3**   : Zasilanie modułów KY-018 i DS1302
* **GND**   : Wspólna masa układu

---

## 📦 Wymagane biblioteki w Arduino IDE
Zainstaluj w **Menedżerze bibliotek** (Ctrl+Shift+I):
1. **Rtc by Makuna** (obsługa układu DS1302).
2. Standardowe biblioteki ESP32 (`EEPROM`, `Arduino`).

---

## ⚙️ Funkcjonalności
1. **Wykrywanie kubeczka**:
   * Jeśli kubeczek jest na miejscu: świeci dioda **zielona (GPIO1)**.
   * Jeśli kubeczka brak: świeci dioda **czerwona (GPIO2)** i podawanie leku jest wstrzymywane (zabezpieczenie przed rozsypaniem tabletek).
2. **Podawanie leków**:
   * Zgodnie z harmonogramem z zegara RTC DS1302.
   * W dowolnej chwili na żądanie przyciskiem na **GPIO19**.
   * Przez komendę `podaj` w Serial Monitorze.
3. **Automatyczne odłączanie cewek silnika**:
   * Po każdym obrocie piny ULN2003 przechodzą w stan niski (`LOW`), co zapobiega nagrzewaniu się silnika 28BYJ-48 i oszczędza energię.
4. **Zapis konfiguracji w pamięci Flash (EEPROM)**:
   * Wszystkie parametry kalibracji (kroki, progi, harmonogram) są zapamiętywane po wyłączeniu zasilania.

---

## 🛠️ Kalibracja i komendy Serial Monitora (115200 baud)
Otwórz Monitor Portu Szeregowego i wpisz:
* `status` - wyświetla stan czujnika, aktualną godzinę i konfigurację.
* `podaj` - natychmiastowe podanie leku.
* `prog <0-4095>` - kalibracja progu odcięcia dla fotorezystora (np. `prog 1800`).
* `inwersja <0|1>` - wybór logiki detekcji (1: zaciemniony = kubek, 0: oświetlony = kubek).
* `kroki <n>` - kalibracja ilości kroków silnika na 1 dawkę (np. `kroki 512` = 1/8 pełnego obrotu, `kroki 2048` = pół obrotu).
* `obroc <n>` - testowy obrót o zadaną liczbę kroków (dodatnia liczba = prawo, ujemna = lewo).
* `wycisz <0|1>` - `wycisz 1` wycisza głośniczek, `wycisz 0` włącza dźwięk.
* `czas <godz> <min> <sek>` - ustawienie godziny w DS1302 (np. `czas 14 30 00`).
* `harmonogram <indeks 0-7> <godz> <min>` - ustawienie zaplanowanej godziny podawania (np. `harmonogram 0 8 0` = 08:00 rano).
