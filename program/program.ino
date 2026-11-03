#include <Arduino.h>
#include <EEPROM.h>
#include <WiFi.h>
#include <WebServer.h>
#include <RtcDS1302.h>
#include <ThreeWire.h>

/* =========================================================================
   DEFINICJA PINÓW ZGODNIE Z OPISEM
   ========================================================================= */
#define PIN_LED_GREEN    1   // Zielona dioda (obecność kubeczka)
#define PIN_LED_RED      2   // Czerwona dioda (brak kubeczka / błąd / stan czuwania)
#define PIN_KY018_ADC    4   // Fotorezystor (KY-018 pin Sygnał S)
#define PIN_RTC_CLK      5   // DS1302 SCLK / CLK
#define PIN_RTC_DAT      6   // DS1302 IO / DAT
#define PIN_RTC_RST      7   // DS1302 CE / RST
#define PIN_STEP_IN1     10  // ULN2003 Driver IN1
#define PIN_STEP_IN2     11  // ULN2003 Driver IN2
#define PIN_STEP_IN3     12  // ULN2003 Driver IN3
#define PIN_STEP_IN4     13  // ULN2003 Driver IN4
#define PIN_BUTTON       19  // Przycisk ręcznego dozowania (do GND z PULLUP)
#define PIN_BUZZER       8   // Głośniczek / buzzer (zgodnie z podłączeniem: GPIO8 / P8)

/* =========================================================================
   DANE PUNKTU DOSTĘPOWEGO (AP) I SERWERA WWW
   ========================================================================= */
const char* AP_SSID = "Podajnik Leków";
const char* AP_PASS = "leki1234";

WebServer server(80);

/* =========================================================================
   STRUKTURA USTAWIEŃ W PAMIĘCI EEPROM
   ========================================================================= */
#define EEPROM_MAGIC 0x4D454435 // Wersja 5 konfiguracji (progi 24h)

struct Config {
  uint32_t magic;
  int ldrThresholdPct;    // Domyślny próg ogólny
  int hourlyThresholds[24]; // Niezależne progi fotorezystora dla każdej z 24 godzin (0..23)
  bool ldrInvert;         // Logika kubka: true = odczyt < próg oznacza kubek
  int slotSteps[8];       // Ilość kroków silnika dla każdego z 8 slotów / przedziałek
  int currentSlot;        // Wybrany aktywny slot (0 - 7)
  int stepDelayMicros;    // Prędkość silnika w us
  bool buzzerMuted;       // Wyciszenie głośniczka
  int scheduleHours[8];   // Harmonogram: godziny (0 - 23) lub -1
  int scheduleMinutes[8]; // Harmonogram: minuty (0 - 59)
  int scheduleSlot[8];    // Który slot (0 - 7) ma zostać podany o danej godzinie
  int scheduleCount;      // Liczba wpisów w harmonogramie
};

Config config;

/* Inicjalizacja zegara czasu rzeczywistego DS1302 */
ThreeWire myWire(PIN_RTC_DAT, PIN_RTC_CLK, PIN_RTC_RST);
RtcDS1302<ThreeWire> Rtc(myWire);

/* Półkrokowa sekwencja sterowania 28BYJ-48 (8 kroków) */
const uint8_t stepSequence[8][4] = {
  {1, 0, 0, 0},
  {1, 1, 0, 0},
  {0, 1, 0, 0},
  {0, 1, 1, 0},
  {0, 0, 1, 0},
  {0, 0, 1, 1},
  {0, 0, 0, 1},
  {1, 0, 0, 1}
};

int currentStepIndex = 0;
int lastDispenseMinute = -1;
int lastDispenseDay = -1; // Zapobiega powtórnemu wywołaniu w tej samej minucie danego dnia
unsigned long lastSensorCheck = 0;
bool lastCupPresent = false;
int cachedLdrPct = 0; // Zapamiętany ostatni pomiar fotorezystora
String lastStatusMessage = "Urządzenie gotowe do pracy.";

// Zmienne do obsługi przypomnienia po 5 minutach (300 000 ms)
bool waitingForCupPickup = false;          // Flaga oczekiwania na zabranie kubka po podaniu
unsigned long dispenseTimestamp = 0;      // Czas podania leku
unsigned long lastReminderAlarm = 0;       // Czas ostatniego odegrania przypomnienia
const unsigned long CUP_TIMEOUT_MS = 300000; // 5 minut (5 * 60 * 1000 ms)

/* =========================================================================
   FUNKCJE SILNIKA KROKOWEGO
   ========================================================================= */
void motorPowerOff() {
  digitalWrite(PIN_STEP_IN1, LOW);
  digitalWrite(PIN_STEP_IN2, LOW);
  digitalWrite(PIN_STEP_IN3, LOW);
  digitalWrite(PIN_STEP_IN4, LOW);
}

void stepMotorSingle(int dir) {
  if (dir > 0) {
    currentStepIndex = (currentStepIndex + 1) % 8;
  } else {
    currentStepIndex = (currentStepIndex + 7) % 8;
  }
  digitalWrite(PIN_STEP_IN1, stepSequence[currentStepIndex][0]);
  digitalWrite(PIN_STEP_IN2, stepSequence[currentStepIndex][1]);
  digitalWrite(PIN_STEP_IN3, stepSequence[currentStepIndex][2]);
  digitalWrite(PIN_STEP_IN4, stepSequence[currentStepIndex][3]);
}

void rotateMotor(int steps, int dir = 1) {
  for (int i = 0; i < steps; i++) {
    stepMotorSingle(dir);
    delayMicroseconds(config.stepDelayMicros > 800 ? config.stepDelayMicros : 1200);
    yield();
  }
  motorPowerOff();
}

/* =========================================================================
   FUNKCJE SYGNALIZACJI I SENSORA (0-100%)
   ========================================================================= */
void beep(int frequency = 2000, int durationMs = 100) {
  if (config.buzzerMuted) return;
  tone(PIN_BUZZER, frequency, durationMs);
}

// Melodyjka grana przy każdym obrocie bębna / podaniu leku
void playDispenseMelody() {
  if (config.buzzerMuted) return;
  // Wesoła melodyjka sukcesu
  const int notes[] = { 1047, 1319, 1568, 2093 }; // C6, E6, G6, C7
  const int durations[] = { 100, 100, 100, 250 };
  for (int i = 0; i < 4; i++) {
    tone(PIN_BUZZER, notes[i], durations[i]);
    delay(durations[i] + 30);
  }
}

// Przeliczenie surowego odczytu ADC ESP32 (0 - 4095) na skalę 0 - 100 punktów
// W momencie pomiaru wyłączamy obie diody LED i czekamy 120 ms,
// po czym zbieramy 50 próbek pomiarowych w celu wygładzenia wszelkich tętnień świetlnych (np. migotanie żarówek 50Hz)
int measureLdrPercentage() {
  digitalWrite(PIN_LED_GREEN, LOW);
  digitalWrite(PIN_LED_RED, LOW);
  delay(120); // Dłuższy czas na pełną relaksację fotorezystora CdS po wygaszeniu diod

  long sum = 0;
  for (int i = 0; i < 50; i++) {
    sum += analogRead(PIN_KY018_ADC);
    delayMicroseconds(400); // 50 próbek x 400us = 20ms (dokładnie 1 pełny okres sieci 50Hz!)
  }
  int raw = sum / 50;
  
  int pct = map(raw, 0, 4095, 0, 100);
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;

  cachedLdrPct = pct;
  return pct;
}

// Wyznaczenie aktualnego progu fotorezystora dla bieżącej godziny
int getCurrentThreshold() {
  RtcDateTime now = Rtc.GetDateTime();
  if (!now.IsValid()) return config.ldrThresholdPct;

  int h = now.Hour();
  if (h >= 0 && h < 24) {
    return config.hourlyThresholds[h];
  }
  return config.ldrThresholdPct;
}

// Sprawdzenie stanu kubeczka
bool evaluateCupState(int pct) {
  int activeThreshold = getCurrentThreshold();
  if (config.ldrInvert) {
    return (pct < activeThreshold);
  } else {
    return (pct >= activeThreshold);
  }
}

bool checkCupPresent() {
  return evaluateCupState(cachedLdrPct);
}

void performSensorMeasurementAndDisplay() {
  int pct = measureLdrPercentage();
  bool cup = evaluateCupState(pct);
  lastCupPresent = cup;
  int activeThresh = getCurrentThreshold();

  // Natychmiastowe włączenie właściwej diody po pomiarze
  if (cup) {
    digitalWrite(PIN_LED_GREEN, HIGH);
    digitalWrite(PIN_LED_RED, LOW);
  } else {
    digitalWrite(PIN_LED_GREEN, LOW);
    digitalWrite(PIN_LED_RED, HIGH);

    // KUBEK ZOSTAŁ ZABRANY! Resetujemy stan alarmu
    if (waitingForCupPickup) {
      waitingForCupPickup = false;
      noTone(PIN_BUZZER);
      lastStatusMessage = "Kubeczek z lekiem został odebrany. Alarm wyłączony.";
      Serial.println("[ALARM] Kubek został odebrany! Zatrzymano powiadomienie.");
    }
  }

  Serial.printf("[SENSOR co 5s] Odczyt: %d pkt, Aktywny prog: %d pkt, Inwersja: %d => Kubek: %s\n", 
                pct, activeThresh, config.ldrInvert, cup ? "OBECNY (ZIELONY)" : "BRAK (CZERWONY)");
}

/* =========================================================================
   LOGIKA PODAWANIA LEKÓW ZE SLOTEM
   ========================================================================= */
bool dispenseMedication(int slotIdx, const char* reason, bool force = false) {
  if (slotIdx < 0 || slotIdx >= 8) slotIdx = config.currentSlot;
  int stepsToRun = config.slotSteps[slotIdx];

  Serial.printf("\n[DOZOWNIK] Podawanie slotu #%d (%d krokow). Powod: %s (force=%d)\n", slotIdx + 1, stepsToRun, reason, force);

  if (!force && !checkCupPresent()) {
    lastStatusMessage = "Brak kubeczka na miejscu! Wstrzymano podawanie.";
    Serial.println("[BLAD] Brak kubeczka!");
    for (int i = 0; i < 3; i++) {
      beep(1000, 150);
      digitalWrite(PIN_LED_RED, HIGH);
      delay(200);
      digitalWrite(PIN_LED_RED, LOW);
      delay(100);
    }
    return false;
  }

  rotateMotor(stepsToRun, 1);

  // Wesoła głośna melodyjka sukcesu po obrocie
  playDispenseMelody();

  config.currentSlot = (slotIdx + 1) % 8; // Przejście do kolejnego slotu
  EEPROM.put(0, config);
  EEPROM.commit();

  // Uruchomienie licznika 5 minut na zabranie kubeczka
  waitingForCupPickup = true;
  dispenseTimestamp = millis();

  lastStatusMessage = String("Podano slot #") + (slotIdx + 1) + " (" + stepsToRun + " kroków). Oczekiwanie na zabranie kubeczka.";
  Serial.printf("[DOZOWNIK] Podano pomyslnie. Oczekiwanie na odebranie leku (alarm za 5 min). Następny slot: #%d\n\n", config.currentSlot + 1);
  return true;
}

/* =========================================================================
   ZARZĄDZANIE PAMIĘCIĄ EEPROM
   ========================================================================= */
void loadConfig() {
  EEPROM.begin(sizeof(Config));
  EEPROM.get(0, config);
  if (config.magic != EEPROM_MAGIC) {
    Serial.println("[EEPROM] Inicjalizacja domyslnych wartosci v5 (24h)...");
    config.magic = EEPROM_MAGIC;
    config.ldrThresholdPct = 40;
    
    // Domyślne wartości dla 24 godzin (nocą ciemniej ~30, za dnia jaśniej ~45-50)
    for (int h = 0; h < 24; h++) {
      if (h >= 6 && h < 12) config.hourlyThresholds[h] = 45;      // Rano
      else if (h >= 12 && h < 18) config.hourlyThresholds[h] = 50; // Popołudnie
      else if (h >= 18 && h < 22) config.hourlyThresholds[h] = 38; // Wieczór
      else config.hourlyThresholds[h] = 30;                        // Noc
    }

    config.ldrInvert = true;
    config.currentSlot = 0;
    config.stepDelayMicros = 1200;
    config.buzzerMuted = false;

    for (int i = 0; i < 8; i++) {
      config.slotSteps[i] = 512;
    }

    config.scheduleCount = 1;
    config.scheduleHours[0] = 8;
    config.scheduleMinutes[0] = 0;
    config.scheduleSlot[0] = 0;
    for (int i = 1; i < 8; i++) {
      config.scheduleHours[i] = -1;
      config.scheduleMinutes[i] = 0;
      config.scheduleSlot[i] = i;
    }
    EEPROM.put(0, config);
    EEPROM.commit();
  }
}

void saveConfig() {
  EEPROM.put(0, config);
  EEPROM.commit();
}

/* =========================================================================
   INTERFEJS WEBUI - DARK THEME (HTML/CSS/JS)
   ========================================================================= */
const char PAGE_MAIN[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pl">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>DOSTAWCA LEKÓW - Dark Panel</title>
  <style>
    :root {
      --bg: #121418;
      --card: #1c1f26;
      --card-border: #2a303c;
      --text: #e2e8f0;
      --text-muted: #94a3b8;
      --primary: #3b82f6;
      --primary-hover: #2563eb;
      --green: #10b981;
      --red: #ef4444;
      --slot-bg: #262c38;
      --slot-active: #1e3a8a;
    }
    * { box-sizing: border-box; }
    body {
      font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif;
      background: var(--bg);
      color: var(--text);
      margin: 0;
      padding: 16px;
    }
    .container { max-width: 650px; margin: 0 auto; }
    h1 {
      text-align: center;
      color: #60a5fa;
      margin: 10px 0 4px 0;
      font-size: 26px;
      letter-spacing: 0.5px;
    }
    p.subtitle {
      text-align: center;
      color: var(--text-muted);
      margin-top: 0;
      margin-bottom: 22px;
      font-size: 14px;
    }
    .card {
      background: var(--card);
      border: 1px solid var(--card-border);
      border-radius: 14px;
      padding: 18px;
      margin-bottom: 18px;
      box-shadow: 0 8px 24px rgba(0,0,0,0.35);
    }
    h2 {
      font-size: 17px;
      margin: 0 0 14px 0;
      border-bottom: 1px solid var(--card-border);
      padding-bottom: 10px;
      color: #93c5fd;
      display: flex;
      align-items: center;
      gap: 8px;
    }
    .status-badge {
      display: inline-block;
      padding: 6px 16px;
      border-radius: 20px;
      font-weight: 700;
      font-size: 13px;
      letter-spacing: 0.5px;
      color: white;
    }
    .badge-cup-yes { background-color: var(--green); box-shadow: 0 0 12px rgba(16,185,129,0.4); }
    .badge-cup-no { background-color: var(--red); box-shadow: 0 0 12px rgba(239,68,68,0.4); }
    .btn {
      display: block;
      width: 100%;
      background: var(--primary);
      color: white;
      border: none;
      padding: 12px;
      border-radius: 8px;
      font-size: 15px;
      font-weight: 600;
      cursor: pointer;
      text-align: center;
      transition: background 0.2s, transform 0.1s;
    }
    .btn:hover { background: var(--primary-hover); }
    .btn:active { transform: scale(0.99); }
    .btn-secondary { background: #374151; }
    .btn-secondary:hover { background: #4b5563; }
    .row {
      display: flex;
      justify-content: space-between;
      align-items: center;
      margin-bottom: 12px;
      font-size: 14px;
    }
    label { font-weight: 500; color: var(--text-muted); }
    input[type="number"], input[type="text"], input[type="time"], select {
      background: #111827;
      border: 1px solid #374151;
      color: #f3f4f6;
      padding: 8px 10px;
      border-radius: 8px;
      font-size: 14px;
      text-align: center;
      outline: none;
    }
    input[type="number"]:focus, select:focus { border-color: var(--primary); }
    
    /* Pasek postępu fotorezystora 0-100 */
    .bar-wrap {
      width: 100%;
      background: #111827;
      height: 18px;
      border-radius: 10px;
      overflow: hidden;
      margin: 10px 0 14px 0;
      border: 1px solid #374151;
      position: relative;
    }
    .bar-fill {
      height: 100%;
      background: linear-gradient(90deg, #10b981, #f59e0b, #ef4444);
      width: 0%;
      transition: width 0.3s ease;
    }
    .bar-thresh-line {
      position: absolute;
      top: 0;
      bottom: 0;
      width: 2px;
      background: #ffffff;
      box-shadow: 0 0 6px #fff;
    }

    /* Siatka 8 slotów / przedziałek */
    .slots-grid {
      display: grid;
      grid-template-columns: repeat(2, 1fr);
      gap: 10px;
      margin-bottom: 14px;
    }
    /* Siatka 24 godzin */
    .hours-grid {
      display: grid;
      grid-template-columns: repeat(4, 1fr);
      gap: 8px;
      margin-bottom: 14px;
    }
    .hour-item {
      background: #111827;
      border: 1px solid #374151;
      padding: 6px 8px;
      border-radius: 6px;
      display: flex;
      flex-direction: column;
      align-items: center;
      gap: 4px;
    }
    .hour-item.active-hour {
      border-color: #60a5fa;
      background: #1e3a8a;
    }
    .hour-label { font-size: 11px; font-weight: 600; color: #93c5fd; }
    .hour-input { width: 55px !important; padding: 4px !important; font-size: 13px !important; }
    .slot-card {
      background: var(--slot-bg);
      border: 1px solid #334155;
      padding: 10px 12px;
      border-radius: 8px;
      display: flex;
      justify-content: space-between;
      align-items: center;
    }
    .slot-card.active-slot {
      border-color: #60a5fa;
      background: var(--slot-active);
    }
    .slot-label { font-size: 13px; font-weight: 600; color: #93c5fd; }
    .slot-steps-in { width: 85px !important; }

    /* Harmonogram */
    .sched-grid {
      display: grid;
      grid-template-columns: repeat(2, 1fr);
      gap: 10px;
      margin-bottom: 12px;
    }
    .sched-item {
      background: #1a202c;
      border: 1px solid #2d3748;
      padding: 8px;
      border-radius: 8px;
      display: flex;
      justify-content: space-between;
      align-items: center;
      gap: 6px;
      font-size: 13px;
    }
    .log-box {
      background: #111827;
      border: 1px solid #1f2937;
      padding: 10px;
      border-radius: 8px;
      font-family: monospace;
      font-size: 13px;
      color: #94a3b8;
    }
  </style>
</head>
<body>
  <div class="container">
    <h1>💊 Podajnik Leków</h1>
    <p class="subtitle">Panel WebUI Dark Mode • IP: 192.168.4.1</p>

    <!-- KARTA 1: STATUS -->
    <div class="card">
      <h2>📊 Bieżący stan</h2>
      <div class="row">
        <span>Status kubeczka:</span>
        <span id="cupBadge" class="status-badge badge-cup-no">Łączenie...</span>
      </div>

      <div class="row" style="margin-bottom: 4px;">
        <span>Fotorezystor KY-018:</span>
        <strong id="valLdr">-- / 100 pkt</strong>
      </div>
      <div class="bar-wrap">
        <div id="barFill" class="bar-fill"></div>
        <div id="barThreshLine" class="bar-thresh-line" style="left: 40%;"></div>
      </div>

      <div class="row">
        <span>Zegar DS1302:</span>
        <strong id="valTime" style="color: #60a5fa;">--:--:--</strong>
      </div>
      <div class="row">
        <span>Kolejny slot do podania:</span>
        <strong id="valNextSlot" style="color: #34d399;">Slot #1</strong>
      </div>
      <div class="row">
        <span>Głośniczek dźwiękowy:</span>
        <button id="btnMuteToggle" class="status-badge" style="border:none; cursor:pointer;" onclick="toggleMute()">Ładowanie...</button>
      </div>
      <div class="row" style="margin-bottom: 4px;">
        <span>Komunikat:</span>
      </div>
      <div id="valStatus" class="log-box">Wczytywanie danych...</div>

      <button class="btn" style="margin-top: 14px;" onclick="dispenseCurrent()">PODAJ AKTUALNY SLOT</button>
      <button class="btn btn-secondary" style="margin-top: 8px;" onclick="manualNextSlot()">OBRÓĆ DO NASTĘPNEGO SLOTU (BEZ SPRAWDZANIA KUBKA)</button>
    </div>

    <!-- KARTA 2: KALIBRACJA SENSORA (DLA KAŻDEJ GODZINY 0-23) -->
    <div class="card">
      <h2>🎯 Kalibracja czujnika dla każdej godziny (00:00 - 23:00)</h2>
      <p style="font-size: 13px; color: var(--text-muted); margin-top: 0;">Ustaw indywidualny próg czułości (0-100) dla każdej z 24 godzin doby. Aktualna godzina jest wyróżniona na niebiesko:</p>
      <form action="/save_sensor" method="POST">
        <div class="hours-grid" id="hoursContainer">
          <!-- Wypełniane dynamicznie przez JS dla 24 godzin -->
        </div>
        <div class="row" style="margin-top: 10px;">
          <label>Inwersja (Zaciemniony = Kubek):</label>
          <input type="checkbox" name="invert" id="inInvert" style="transform: scale(1.3); accent-color: var(--primary);">
        </div>
        <button type="submit" class="btn btn-secondary" style="margin-top: 10px;">Zapisz progi 24h</button>
      </form>
    </div>

    <!-- KARTA 3: KALIBRACJA 8 SLOTÓW (PRZEDZIAŁEK) -->
    <div class="card">
      <h2>⚙️ Kalibracja kroków silnika dla 8 slotów</h2>
      <p style="font-size: 13px; color: var(--text-muted); margin-top: 0;">Ustaw indywidualną liczbę kroków silnika dla każdej przedziałki:</p>
      <form action="/save_slots" method="POST">
        <div class="slots-grid">
          <div class="slot-card" id="sc0"><span class="slot-label">Slot #1</span><input type="number" name="s0" id="s0" class="slot-steps-in" min="1" max="16384"></div>
          <div class="slot-card" id="sc1"><span class="slot-label">Slot #2</span><input type="number" name="s1" id="s1" class="slot-steps-in" min="1" max="16384"></div>
          <div class="slot-card" id="sc2"><span class="slot-label">Slot #3</span><input type="number" name="s2" id="s2" class="slot-steps-in" min="1" max="16384"></div>
          <div class="slot-card" id="sc3"><span class="slot-label">Slot #4</span><input type="number" name="s3" id="s3" class="slot-steps-in" min="1" max="16384"></div>
          <div class="slot-card" id="sc4"><span class="slot-label">Slot #5</span><input type="number" name="s4" id="s4" class="slot-steps-in" min="1" max="16384"></div>
          <div class="slot-card" id="sc5"><span class="slot-label">Slot #6</span><input type="number" name="s5" id="s5" class="slot-steps-in" min="1" max="16384"></div>
          <div class="slot-card" id="sc6"><span class="slot-label">Slot #7</span><input type="number" name="s6" id="s6" class="slot-steps-in" min="1" max="16384"></div>
          <div class="slot-card" id="sc7"><span class="slot-label">Slot #8</span><input type="number" name="s7" id="s7" class="slot-steps-in" min="1" max="16384"></div>
        </div>
        <div class="row">
          <label>Wyciszenie głośniczka:</label>
          <input type="checkbox" name="muted" id="inMuted" style="transform: scale(1.3); accent-color: var(--primary);">
        </div>
        <button type="submit" class="btn btn-secondary">Zapisz ustawienia slotów</button>
      </form>

      <div style="display: flex; gap: 8px; margin-top: 10px;">
        <button type="button" class="btn btn-secondary" style="font-size: 13px;" onclick="testRotate(64)">+64</button>
        <button type="button" class="btn btn-secondary" style="font-size: 13px;" onclick="testRotate(-64)">-64</button>
        <button type="button" class="btn btn-secondary" style="font-size: 13px;" onclick="testRotate(256)">+256</button>
        <button type="button" class="btn btn-secondary" style="font-size: 13px;" onclick="testRotate(-256)">-256</button>
      </div>

      <!-- USTAWIANIE PUNKTU ZERO -->
      <div style="margin-top: 15px; padding-top: 12px; border-top: 1px solid var(--card-border);">
        <label style="color: #93c5fd; font-weight: 600;">📍 Punkt zerowy podajnika:</label>
        <p style="font-size: 12px; color: var(--text-muted); margin: 4px 0 10px 0;">Użyj przycisków powyżej aby ustawić komorę na pozycji wyjściowej, a następnie zapisz to jako Punkt Zero (Slot #1):</p>
        <div style="display: flex; gap: 10px;">
          <button type="button" class="btn" style="background: #059669;" onclick="setZeroPoint()">USTAW AKTUALNĄ POZYCJĘ JAKO PUNKT ZERO (SLOT #1)</button>
        </div>
      </div>
    </div>

    <!-- KARTA 4: HARMONOGRAM -->
    <div class="card">
      <h2>⏰ Codzienny harmonogram podawania</h2>
      <p style="font-size: 13px; color: var(--text-muted); margin-top: 0;">Podajnik obróci się automatycznie każdego dnia dokładnie o wybranej godzinie:</p>
      <form action="/save_schedule" method="POST">
        <div class="row" style="justify-content: flex-start; gap: 15px;">
          <label style="font-size: 15px; color: #fff;">Godzina podania:</label>
          <input type="time" name="daily_time" id="dailyTime" style="font-size: 16px; padding: 10px; width: 140px;" required>
        </div>
        <button type="submit" class="btn btn-secondary" style="margin-top: 10px;">Zapisz codzienną godzinę</button>
      </form>
    </div>

    <!-- KARTA 5: ZEGAR -->
    <div class="card">
      <h2>🕒 Synchronizacja czasu</h2>
      <button class="btn btn-secondary" onclick="syncBrowserTime()">Pobierz czas z telefonu / komputera</button>
    </div>
  </div>

  <script>
    let firstLoad = true;

    // Generowanie pól dla 24 godzin
    function initHourGrid() {
      let cont = document.getElementById('hoursContainer');
      cont.innerHTML = '';
      for (let h = 0; h < 24; h++) {
        let hStr = (h < 10 ? '0' : '') + h + ':00';
        let div = document.createElement('div');
        div.className = 'hour-item';
        div.id = 'hCard' + h;
        div.innerHTML = '<span class="hour-label">' + hStr + '</span>' +
                        '<input type="number" name="h' + h + '" id="inH' + h + '" class="hour-input" min="0" max="100" required>';
        cont.appendChild(div);
      }
    }

    function refreshData() {
      fetch('/api/data')
        .then(r => r.json())
        .then(d => {
          document.getElementById('cupBadge').className = 'status-badge ' + (d.cup ? 'badge-cup-yes' : 'badge-cup-no');
          document.getElementById('cupBadge').innerText = d.cup ? 'KUBEK NA MIEJSCU' : 'BRAK KUBECZKA';
          
          document.getElementById('valLdr').innerText = d.ldr + ' / 100 pkt';
          document.getElementById('barFill').style.width = d.ldr + '%';
          document.getElementById('barThreshLine').style.left = d.activeThresh + '%';

          document.getElementById('valTime').innerText = d.time;
          document.getElementById('valNextSlot').innerText = 'Slot #' + (d.curSlot + 1);
          document.getElementById('valStatus').innerText = d.statusMsg;

          // Wyróżnienie aktualnej godziny w siatce 24h
          let curH = parseInt(d.time.split(':')[0]);
          for (let h = 0; h < 24; h++) {
            let el = document.getElementById('hCard' + h);
            if (el) {
              if (h === curH) el.classList.add('active-hour');
              else el.classList.remove('active-hour');
            }
          }

          // Przycisk szybkiego wyciszenia
          let muteBtn = document.getElementById('btnMuteToggle');
          if (d.muted) {
            muteBtn.innerText = '🔇 WYCISZONY (Kliknij, aby włączyć)';
            muteBtn.style.backgroundColor = '#ef4444';
          } else {
            muteBtn.innerText = '🔊 DŹWIĘK WŁĄCZONY (Kliknij, aby wyciszyć)';
            muteBtn.style.backgroundColor = '#10b981';
          }

          // Podświetlenie aktywnego slotu
          for (let i = 0; i < 8; i++) {
            let el = document.getElementById('sc' + i);
            if (el) {
              if (i === d.curSlot) el.classList.add('active-slot');
              else el.classList.remove('active-slot');
            }
          }

          if (firstLoad) {
            for (let h = 0; h < 24; h++) {
              let inH = document.getElementById('inH' + h);
              if (inH && d.hourlyThresh && d.hourlyThresh[h] !== undefined) {
                inH.value = d.hourlyThresh[h];
              }
            }
            document.getElementById('inInvert').checked = d.invert;
            document.getElementById('inMuted').checked = d.muted;

            for (let i = 0; i < 8; i++) {
              document.getElementById('s' + i).value = d.slotSteps[i];
            }
            if (d.dailyTime) {
              document.getElementById('dailyTime').value = d.dailyTime;
            }
            firstLoad = false;
          }
        });
    }

    function dispenseCurrent() {
      fetch('/api/dispense', { method: 'POST' })
        .then(r => r.text())
        .then(msg => {
          alert(msg);
          refreshData();
        });
    }

    function manualNextSlot() {
      fetch('/api/force_advance', { method: 'POST' })
        .then(r => r.text())
        .then(msg => {
          alert(msg);
          refreshData();
        });
    }

    function toggleMute() {
      fetch('/api/toggle_mute', { method: 'POST' })
        .then(() => refreshData());
    }

    function testRotate(st) {
      fetch('/api/rotate?steps=' + st, { method: 'POST' })
        .then(() => refreshData());
    }

    function setZeroPoint() {
      if (confirm('Czy chcesz ustawić bieżącą pozycję silnika jako Punkt Zero (Slot #1)?')) {
        fetch('/api/set_zero', { method: 'POST' })
          .then(r => r.text())
          .then(msg => {
            alert(msg);
            refreshData();
          });
      }
    }

    function syncBrowserTime() {
      const now = new Date();
      fetch('/api/set_time?h=' + now.getHours() + '&m=' + now.getMinutes() + '&s=' + now.getSeconds(), { method: 'POST' })
        .then(r => r.text())
        .then(msg => {
          alert(msg);
          refreshData();
        });
    }

    setInterval(refreshData, 1500);
    window.onload = function() {
      initHourGrid();
      refreshData();
    };
  </script>
</body>
</html>
)rawliteral";

/* =========================================================================
   ENDPOINTY SERWERA WWW
   ========================================================================= */
void handleRoot() {
  server.send_P(200, "text/html; charset=utf-8", PAGE_MAIN);
}

void handleApiData() {
  RtcDateTime now = Rtc.GetDateTime();
  char timeBuf[16];
  snprintf(timeBuf, sizeof(timeBuf), "%02u:%02u:%02u", now.Hour(), now.Minute(), now.Second());

  char dailyBuf[8];
  if (config.scheduleHours[0] >= 0) {
    snprintf(dailyBuf, sizeof(dailyBuf), "%02d:%02d", config.scheduleHours[0], config.scheduleMinutes[0]);
  } else {
    snprintf(dailyBuf, sizeof(dailyBuf), "08:00");
  }

  int pct = cachedLdrPct;
  bool cup = checkCupPresent();
  int activeThresh = getCurrentThreshold();

  String json = "{";
  json += "\"cup\":" + String(cup ? "true" : "false") + ",";
  json += "\"ldr\":" + String(pct) + ",";
  json += "\"activeThresh\":" + String(activeThresh) + ",";
  json += "\"hourlyThresh\":[";
  for (int h = 0; h < 24; h++) {
    json += String(config.hourlyThresholds[h]);
    if (h < 23) json += ",";
  }
  json += "],";
  json += "\"invert\":" + String(config.ldrInvert ? "true" : "false") + ",";
  json += "\"curSlot\":" + String(config.currentSlot) + ",";
  json += "\"muted\":" + String(config.buzzerMuted ? "true" : "false") + ",";
  json += "\"time\":\"" + String(timeBuf) + "\",";
  json += "\"dailyTime\":\"" + String(dailyBuf) + "\",";
  json += "\"statusMsg\":\"" + lastStatusMessage + "\",";
  
  json += "\"slotSteps\":[";
  for (int i = 0; i < 8; i++) {
    json += String(config.slotSteps[i]);
    if (i < 7) json += ",";
  }
  json += "]";
  json += "}";

  server.send(200, "application/json", json);
}

void handleApiDispense() {
  bool ok = dispenseMedication(config.currentSlot, "Żądanie z WebUI");
  if (ok) {
    server.send(200, "text/plain; charset=utf-8", "Lek został podany!");
  } else {
    server.send(400, "text/plain; charset=utf-8", "BŁĄD: Brak kubeczka na miejscu!");
  }
}

void handleApiForceAdvance() {
  int slotIdx = config.currentSlot;
  int stepsToRun = config.slotSteps[slotIdx];
  rotateMotor(stepsToRun, 1);
  playDispenseMelody();
  config.currentSlot = (slotIdx + 1) % 8;
  saveConfig();
  lastStatusMessage = String("Obrócono bęben do Slot #") + (config.currentSlot + 1);
  server.send(200, "text/plain; charset=utf-8", "Wykonano pełny obrót do następnego slotu!");
}

void handleApiToggleMute() {
  config.buzzerMuted = !config.buzzerMuted;
  saveConfig();
  lastStatusMessage = config.buzzerMuted ? "Głośniczek został wyciszony." : "Dźwięk głośniczka włączony.";
  server.send(200, "text/plain", "OK");
}

void handleApiRotate() {
  if (server.hasArg("steps")) {
    int st = server.arg("steps").toInt();
    rotateMotor(abs(st), st >= 0 ? 1 : -1);
    lastStatusMessage = String("Wykonano obrót testowy o ") + st + " kroków.";
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Brak parametru");
  }
}

void handleApiSetZero() {
  config.currentSlot = 0; // Ustawienie Slot #1 jako punkt wyjściowy
  saveConfig();
  lastStatusMessage = "Zresetowano pozycję! Ustawiono Punkt Zero na Slot #1.";
  server.send(200, "text/plain; charset=utf-8", "Ustawiono aktualną pozycję jako Punkt Zero (Slot #1)!");
}

void handleApiSetTime() {
  if (server.hasArg("h") && server.hasArg("m") && server.hasArg("s")) {
    int h = server.arg("h").toInt();
    int m = server.arg("m").toInt();
    int s = server.arg("s").toInt();
    RtcDateTime cur = Rtc.GetDateTime();
    RtcDateTime dt(cur.Year() < 2020 ? 2026 : cur.Year(), 
                   cur.Month() == 0 ? 1 : cur.Month(), 
                   cur.Day() == 0 ? 1 : cur.Day(), 
                   h, m, s);
    Rtc.SetDateTime(dt);
    lastStatusMessage = String("Czas zsynchronizowany: ") + h + ":" + m + ":" + s;
    server.send(200, "text/plain; charset=utf-8", "Czas został zsynchronizowany!");
  } else {
    server.send(400, "text/plain", "Błędne dane");
  }
}

void handleSaveSensor() {
  for (int h = 0; h < 24; h++) {
    String argName = "h" + String(h);
    if (server.hasArg(argName)) {
      config.hourlyThresholds[h] = server.arg(argName).toInt();
    }
  }
  config.ldrInvert = server.hasArg("invert");
  saveConfig();
  performSensorMeasurementAndDisplay(); // Natychmiastowe przeliczenie i zaktualizowanie diod!
  lastStatusMessage = "Zaktualizowano progi czujnika dla 24 godzin doby.";
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleSaveSlots() {
  for (int i = 0; i < 8; i++) {
    String argName = "s" + String(i);
    if (server.hasArg(argName)) {
      config.slotSteps[i] = server.arg(argName).toInt();
    }
  }
  config.buzzerMuted = server.hasArg("muted");
  saveConfig();
  lastStatusMessage = "Zapisano kalibrację kroków dla 8 slotów.";
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleSaveSchedule() {
  if (server.hasArg("daily_time")) {
    String val = server.arg("daily_time");
    int colon = val.indexOf(':');
    if (colon > 0) {
      config.scheduleHours[0] = val.substring(0, colon).toInt();
      config.scheduleMinutes[0] = val.substring(colon + 1).toInt();
      config.scheduleCount = 1;
      saveConfig();
      lastStatusMessage = "Zapisano codzienną godzinę podawania: " + val;
    }
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

/* =========================================================================
   HARMONOGRAM PODAWANIA (CODZIENNY)
   ========================================================================= */
void checkSchedule() {
  RtcDateTime now = Rtc.GetDateTime();
  if (!now.IsValid()) return;

  // Reset flagi minuty gdy zmieniła się minuta, aby harmonogram działał niezawodnie
  if (now.Minute() != lastDispenseMinute) {
    lastDispenseMinute = -1;
  }

  // Jeśli w tej minucie już podano lek, czekamy na kolejną minutę
  if (lastDispenseMinute == now.Minute()) return;

  // Sprawdzenie codziennej godziny
  if (config.scheduleHours[0] >= 0) {
    if (config.scheduleHours[0] == now.Hour() && config.scheduleMinutes[0] == now.Minute()) {
      lastDispenseMinute = now.Minute();
      char buffer[32];
      snprintf(buffer, sizeof(buffer), "Codzienny obrót o [%02d:%02d]", now.Hour(), now.Minute());
      Serial.printf("[RTC] Uruchomiono codzienny harmonogram o %02d:%02d\n", now.Hour(), now.Minute());
      dispenseMedication(config.currentSlot, buffer);
    }
  }
}

/* =========================================================================
   PRZYCISK FIZYCZNY
   ========================================================================= */
void handleButton() {
  static unsigned long lastDebounceTime = 0;
  static int lastButtonState = HIGH;
  int reading = digitalRead(PIN_BUTTON);

  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > 50) {
    static int buttonState = HIGH;
    if (reading != buttonState) {
      buttonState = reading;
      if (buttonState == LOW) {
        Serial.println("[PRZYCISK] Wciśnięto przycisk fizyczny! Wymuszanie obrotu...");
        dispenseMedication(config.currentSlot, "Przycisk fizyczny GPIO19", true); // force=true (obrót ZAWSZE)
      }
    }
  }
  lastButtonState = reading;
}

/* =========================================================================
   SETUP & LOOP
   ========================================================================= */
void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_LED_RED, OUTPUT);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_KY018_ADC, INPUT);
  pinMode(PIN_BUZZER, OUTPUT);

  pinMode(PIN_STEP_IN1, OUTPUT);
  pinMode(PIN_STEP_IN2, OUTPUT);
  pinMode(PIN_STEP_IN3, OUTPUT);
  pinMode(PIN_STEP_IN4, OUTPUT);
  motorPowerOff();

  loadConfig();

  Rtc.Begin();
  if (!Rtc.IsDateTimeValid()) {
    RtcDateTime compiled = RtcDateTime(__DATE__, __TIME__);
    Rtc.SetDateTime(compiled);
  }
  if (Rtc.GetIsWriteProtected()) {
    Rtc.SetIsWriteProtected(false);
  }
  if (!Rtc.GetIsRunning()) {
    Rtc.SetIsRunning(true);
  }

  WiFi.mode(WIFI_AP);
  IPAddress local_ip(192, 168, 4, 1);
  IPAddress gateway(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(local_ip, gateway, subnet);
  WiFi.softAP(AP_SSID, AP_PASS);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/data", HTTP_GET, handleApiData);
  server.on("/api/dispense", HTTP_POST, handleApiDispense);
  server.on("/api/force_advance", HTTP_POST, handleApiForceAdvance);
  server.on("/api/toggle_mute", HTTP_POST, handleApiToggleMute);
  server.on("/api/rotate", HTTP_POST, handleApiRotate);
  server.on("/api/set_zero", HTTP_POST, handleApiSetZero);
  server.on("/api/set_time", HTTP_POST, handleApiSetTime);
  server.on("/save_sensor", HTTP_POST, handleSaveSensor);
  server.on("/save_slots", HTTP_POST, handleSaveSlots);
  server.on("/save_schedule", HTTP_POST, handleSaveSchedule);
  server.begin();

  // Pierwszy pomiar z wygaszonymi diodami
  performSensorMeasurementAndDisplay();
}

// Obsługa alarmu przypomnienia o nieodebranym kubku po 5 minutach
void checkCupReminderAlarm() {
  if (!waitingForCupPickup) return;

  // Sprawdzamy czy minęło 5 minut od podania
  if (millis() - dispenseTimestamp >= CUP_TIMEOUT_MS) {
    // Sprawdzamy czy kubek nadal stoi
    if (checkCupPresent()) {
      // Co 2 sekundy gramy głośną melodyjkę przypominającą
      if (millis() - lastReminderAlarm >= 2000) {
        lastReminderAlarm = millis();
        lastStatusMessage = "UWAGA: Lek czeka od ponad 5 minut! Odbierz kubeczek.";
        Serial.println("[PRZYPOMNIENIE] Męczący alarm: minęło 5 minut, a kubek nadal stoi!");
        playDispenseMelody();
      }
    } else {
      // Kubek został zabrany
      waitingForCupPickup = false;
      noTone(PIN_BUZZER);
    }
  }
}

void loop() {
  server.handleClient();

  // Pomiar fotorezystora wykonywany dokładnie co 5 sekund (5000 ms)
  // W momencie pomiaru obie diody gasną, po czym zapala się właściwa dioda
  if (millis() - lastSensorCheck >= 5000) {
    lastSensorCheck = millis();
    performSensorMeasurementAndDisplay();
  }

  // Sprawdzanie przypomnienia 5-minutowego
  checkCupReminderAlarm();

  handleButton();
  checkSchedule();
  yield();
}
