/*
 * ArduRoomba - Nano V3.0 + CC2540 BLE - Sketch Adattato
 * ====================================================
 * Funzionalità:
 *   - Controllo movimento, motori, LED e audio via Bluetooth Low Energy (BLE)
 *   - Funziona come "Serial Bridge": usa i comandi via Serial standard
 *   - Compatibile con architettura AVR (ATmega328P)
 *
 * Cablaggio Roomba → Nano + CC2540:
 *   Mini-DIN Pin 4 (TX Roomba) → D2  (RX Arduino, SoftwareSerial)
 *   Mini-DIN Pin 3 (RX Roomba) → D3  (TX Arduino, SoftwareSerial)
 *   Mini-DIN Pin 5 (BRC/DD)    → D4
 *   Mini-DIN Pin 6/7 (GND)     → GND
 *
 * =====================================================================
 * IMPORTANTE - Come collegarsi via BLE:
 * =====================================================================
 * Il CC2540 è BLE (Bluetooth Low Energy), NON Bluetooth classico.
 * NON si accoppia come un normale dispositivo Bluetooth dal pannello
 * di sistema. Serve un'app con supporto BLE UART (profilo GATT).
 *
 * APP CONSIGLIATE:
 *   Android : "Serial Bluetooth Terminal" di Kai Morich (Google Play)
 *             → Aggiungi dispositivo → scansione BLE → cerca "ArduRoomba"
 *   PC/Win  : "nRF Connect for Desktop" (Nordic Semiconductor, gratuito)
 *             oppure "BLE Serial" o "LightBlue" su Microsoft Store
 *   iOS/Mac : "LightBlue" (Punch Through) oppure "nRF Connect" mobile
 *
 * Il nome BLE del dispositivo viene impostato via AT all'avvio (vedi
 * BLE_DEVICE_NAME sotto). Se il modulo non risponde agli AT, il nome
 * resterà quello di fabbrica (es. "HMSoft", "CC2540", "BT05" ecc.).
 *
 * BAUD RATE: Il CC2540 deve essere configurato a BLE_BAUD (default 9600).
 *   Se il modulo non risponde, prova a cambiare BLE_BAUD in 38400 o 115200.
 * =====================================================================
 */

#include "ArduRoomba.h"

// ---------------------------------------------------------------------------
// Configurazione BLE (CC2540)
// ---------------------------------------------------------------------------
// Nome che apparirà nelle scansioni BLE delle app (max 20 caratteri)
#define BLE_DEVICE_NAME   "ArduRoomba"
// Baud rate: deve corrispondere a quello impostato sul modulo CC2540.
// Valori comuni: 9600, 38400, 115200
// Se il modulo non risponde agli AT, prova gli altri valori.
#define BLE_BAUD          9600

// ---------------------------------------------------------------------------
// Configurazione pin
// ---------------------------------------------------------------------------
static const int PIN_RX  = 2;
static const int PIN_TX  = 3;
static const int PIN_BRC = 4;

// ---------------------------------------------------------------------------
// Oggetti principali
// ---------------------------------------------------------------------------
ArduRoomba roomba(PIN_RX, PIN_TX, PIN_BRC);

// ---------------------------------------------------------------------------
// Stato interno
// ---------------------------------------------------------------------------
bool      mainBrushOn  = false;
bool      sideBrushOn  = false;
bool      vacuumOn     = false;
bool      safetyActive = true;   // ferma se bumper/cliff
unsigned long lastStatusMs = 0;
static const unsigned long STATUS_INTERVAL_MS = 1000; // Ridotto per AVR

// ---------------------------------------------------------------------------
// Helper: applica stato motori corrente al Roomba
// ---------------------------------------------------------------------------
void applyMotors() {
  roomba.setMotors(mainBrushOn, sideBrushOn, vacuumOn);
}

// ---------------------------------------------------------------------------
// Helper: split di una stringa su un delimitatore
// ---------------------------------------------------------------------------
String splitToken(const String& s, char delim, int index) {
  int found = 0, start = 0;
  for (int i = 0; i <= (int)s.length(); i++) {
    if (i == (int)s.length() || s[i] == delim) {
      if (found == index) return s.substring(start, i);
      found++;
      start = i + 1;
    }
  }
  return "";
}

int countTokens(const String& s, char delim) {
  if (s.length() == 0) return 0;
  int n = 1;
  for (int i = 0; i < (int)s.length(); i++) if (s[i] == delim) n++;
  return n;
}

// ---------------------------------------------------------------------------
// Esegui un passo di sequenza
// ---------------------------------------------------------------------------
bool executeStep(const String& action, int speed, int duration) {
  if      (action == "forward")   { roomba.moveForward(speed > 0 ? speed : 200);   if (duration > 0) { delay(duration); roomba.stop(); } }
  else if (action == "backward")  { roomba.moveBackward(speed > 0 ? speed : 200);  if (duration > 0) { delay(duration); roomba.stop(); } }
  else if (action == "left")      { roomba.turnLeft(speed > 0 ? speed : 150);       if (duration > 0) { delay(duration); roomba.stop(); } }
  else if (action == "right")     { roomba.turnRight(speed > 0 ? speed : 150);      if (duration > 0) { delay(duration); roomba.stop(); } }
  else if (action == "spinleft")  { roomba.spinLeft(speed > 0 ? speed : 150);       if (duration > 0) { delay(duration); roomba.stop(); } }
  else if (action == "spinright") { roomba.spinRight(speed > 0 ? speed : 150);      if (duration > 0) { delay(duration); roomba.stop(); } }
  else if (action == "fwd_left")  { roomba.movement().driveDirect(speed, speed/3);             if (duration > 0) { delay(duration); roomba.stop(); } }
  else if (action == "fwd_right") { roomba.movement().driveDirect(speed/3, speed);             if (duration > 0) { delay(duration); roomba.stop(); } }
  else if (action == "bwd_left")  { roomba.movement().driveDirect(-speed, -speed/3);           if (duration > 0) { delay(duration); roomba.stop(); } }
  else if (action == "bwd_right") { roomba.movement().driveDirect(-speed/3, -speed);           if (duration > 0) { delay(duration); roomba.stop(); } }
  else if (action == "stop")      { roomba.stop(); }
  else return false;
  return true;
}

void handleSequence(const String& payload) {
  int stepCount = countTokens(payload, ';');
  for (int i = 0; i < stepCount; i++) {
    String step   = splitToken(payload, ';', i);
    String action = splitToken(step, ',', 0);
    int    speed  = splitToken(step, ',', 1).toInt();
    int    dur    = splitToken(step, ',', 2).toInt();
    action.toLowerCase();
    if (safetyActive && roomba.isBumperPressed()) {
      roomba.stop();
      return;
    }
    executeStep(action, speed, dur);
  }
}

void handleLED(const String& payload) {
  bool debris = splitToken(payload, ',', 0).toInt() == 1;
  bool spot   = splitToken(payload, ',', 1).toInt() == 1;
  bool dock   = splitToken(payload, ',', 2).toInt() == 1;
  bool check  = splitToken(payload, ',', 3).toInt() == 1;
  roomba.setLED(debris, spot, dock, check);
}

void handlePowerLED(const String& payload) {
  int color     = splitToken(payload, ',', 0).toInt();
  int intensity = splitToken(payload, ',', 1).toInt();
  roomba.setPowerLED(constrain(color, 0, 255), constrain(intensity, 0, 255));
}

int noteNameToMidi(const String& name) {
  if (name == "C3")  return 48; if (name == "D3")  return 50;
  if (name == "E3")  return 52; if (name == "F3")  return 53;
  if (name == "G3")  return 55; if (name == "A3")  return 57;
  if (name == "B3")  return 59;
  if (name == "C4")  return 60; if (name == "D4")  return 62;
  if (name == "E4")  return 64; if (name == "F4")  return 65;
  if (name == "G4")  return 67; if (name == "A4")  return 69;
  if (name == "B4")  return 71;
  if (name == "C5")  return 72; if (name == "D5")  return 74;
  if (name == "E5")  return 76; if (name == "F5")  return 77;
  if (name == "G5")  return 79; if (name == "A5")  return 81;
  return name.toInt();
}

void handleSong(const String& payload) {
  if      (payload == "startup") roomba.actuators().playStartupSong();
  else if (payload == "happy")   roomba.actuators().playHappySong();
  else if (payload == "sad")     roomba.actuators().playSadSong();
  else if (payload == "alert")   roomba.actuators().playAlertSong();
  else if (payload == "beep")    roomba.beep();
  else if (payload.startsWith("custom,")) {
    String notes = payload.substring(7);
    int tokenCount = countTokens(notes, ',');
    int noteCount  = constrain(tokenCount / 2, 1, 16);
    SongNote song[16];
    for (int i = 0; i < noteCount; i++) {
      String noteName = splitToken(notes, ',', i * 2);
      int    duration = splitToken(notes, ',', i * 2 + 1).toInt();
      song[i] = SongNote(noteNameToMidi(noteName), duration);
    }
    roomba.actuators().defineSong(0, song, noteCount);
    delay(50);
    roomba.actuators().playSong(0);
  }
}

String buildStatusString() {
  RoombaSensorData data = roomba.sensors().readAll();
  return String(data.battery.voltage)  + ":" +
         String(data.battery.current)  + ":" +
         String(roomba.getBatteryPercent())   + ":" +
         String(data.bumper.anyBumper()) + ":" +
         String(data.bumper.left)  + ":" +
         String(data.bumper.right) + ":" +
         String(data.wall.wall)     + ":" +
         String(data.cliff.anyCliff())    + ":" +
         String(data.battery.isLow())   + ":" +
         String(data.battery.isCritical())  + ":" +
         String(mainBrushOn) + ":" +
         String(sideBrushOn) + ":" +
         String(vacuumOn);
}

void handleCommand(String cmd) {
  cmd.trim();
  if (cmd.length() == 0) return;

  // Echo del comando ricevuto: utile per verificare che la connessione BLE funzioni
  Serial.println("ACK:" + cmd);
  
  int sep = cmd.indexOf(':');
  String action = (sep >= 0) ? cmd.substring(0, sep) : cmd;
  String payload= (sep >= 0) ? cmd.substring(sep + 1) : "";
  action.toLowerCase();

  if (action == "forward" || action == "backward" || action == "left" || action == "right" || 
      action == "spinleft" || action == "spinright" || action == "stop" ||
      action == "fwd_left" || action == "fwd_right" || action == "bwd_left" || action == "bwd_right") {
    executeStep(action, payload.toInt(), 0);
  }
  else if (action == "seq")      handleSequence(payload);
  else if (action == "clean")    roomba.startCleaning();
  else if (action == "spot")     roomba.spotClean();
  else if (action == "dock")     roomba.dock();
  else if (action == "motor_main") { mainBrushOn = (payload.toInt() == 1); applyMotors(); }
  else if (action == "motor_side") { sideBrushOn = (payload.toInt() == 1); applyMotors(); }
  else if (action == "motor_vac")  { vacuumOn = (payload.toInt() == 1); applyMotors(); }
  else if (action == "motors_all") { mainBrushOn = sideBrushOn = vacuumOn = (payload.toInt() == 1); applyMotors(); }
  else if (action == "led")       handleLED(payload);
  else if (action == "powerled")  handlePowerLED(payload);
  else if (action == "song")      handleSong(payload);
  else if (action == "mode") {
    if      (payload == "safe")    roomba.actuators().setSafeMode();
    else if (payload == "full")    roomba.actuators().setFullMode();
    else if (payload == "passive") roomba.end();
  }
  else if (action == "safety") {
    safetyActive = (payload.toInt() == 1);
    roomba.enableSafety(safetyActive);
  }
  else if (action == "status") {
    Serial.println(buildStatusString());
  }
}

// ---------------------------------------------------------------------------
// Configura il modulo CC2540 via comandi AT (eseguito una sola volta in setup)
// Il modulo accetta AT solo quando NON è connesso a nessun dispositivo.
// ---------------------------------------------------------------------------
void configureBLE() {
  // Attendi che il modulo sia pronto dopo l'accensione
  delay(1000);

  // Alcuni moduli richiedono \r\n, altri no. Proviamo a inviare AT per "svegliarlo"
  Serial.print("AT\r\n"); delay(200);
  Serial.print("AT");     delay(200);

  // Imposta il nome
  Serial.print("AT+NAME" BLE_DEVICE_NAME "\r\n"); delay(300);
  Serial.print("AT+NAME=" BLE_DEVICE_NAME "\r\n"); delay(300);

  // Ruolo: 0 = Slave (Peripheral)
  Serial.print("AT+ROLE0\r\n"); delay(200);
  
  // Tipo: 0 = Connectable (molto importante se non si collega)
  Serial.print("AT+TYPE0\r\n"); delay(200);

  // Intervallo di advertising (default 100ms)
  Serial.print("AT+ADVI0\r\n"); delay(200);
  
  // Forza il baud rate a 9600 se non lo è già (opzionale, pericoloso se sbagliato)
  // Serial.print("AT+BAUD0\r\n"); delay(200);

  // Reset del modulo per applicare le modifiche
  Serial.print("AT+RESET\r\n");
  delay(1200);
}

// ---------------------------------------------------------------------------
// SETUP
// ---------------------------------------------------------------------------
void setup() {
  // Avvia la Serial hardware alla velocità del modulo BLE.
  // IMPORTANTE: BLE_BAUD deve corrispondere al baud del CC2540.
  // Se non riesci a connetterti, prova a cambiare BLE_BAUD in testa al file.
  Serial.begin(BLE_BAUD);

  // Configura il modulo BLE (nome, ruolo, reset)
  // Questa funzione invia comandi AT prima di entrare in modalità dati.
  // Se il modulo non supporta AT o è già configurato, i comandi vengono ignorati.
  configureBLE();

  // Dopo il reset del BLE, riavvia la Serial per sicurezza
  Serial.begin(BLE_BAUD);
  delay(200);

  // Inizializza Roomba (SoftwareSerial su D2/D3)
  roomba.begin(115200);

  // Attiva la modalità Safe (obbligatorio per usare motori e attuatori)
  roomba.actuators().setSafeMode();
  roomba.enableSafety(safetyActive);

  // Messaggio di avvio: visibile sia su monitor seriale USB che via BLE
  Serial.println("OK:ArduRoomba pronto - " BLE_DEVICE_NAME);
}

// ---------------------------------------------------------------------------
// LOOP
// ---------------------------------------------------------------------------
void loop() {
  // Leggi comandi dalla Serial (che riceve anche dal Bluetooth)
  if (Serial.available() > 0) {
    String cmd = Serial.readStringUntil('\n');
    handleCommand(cmd);
  }

  // Safety automatico
  if (safetyActive) {
    roomba.updateSafety();
  }

  // Aggiornamento status periodico su Serial/BLE
  if (millis() - lastStatusMs >= STATUS_INTERVAL_MS) {
    lastStatusMs = millis();
    // Inviamo lo status automatico così il controller HTML vede i dati
    Serial.println(buildStatusString());
  }
}
