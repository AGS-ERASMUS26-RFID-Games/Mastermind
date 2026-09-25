/* =========================================================================
   4 RFID-Reader (RC522) mit Farbzuordnung + 16x16 LED-Matrix
   -------------------------------------------------------------------------
   16 Chips, jeweils 4 pro Farbe (Rot / Gruen / Blau / Gelb).

   Alle Farben stehen als RGB-Werte im Array farben[]:
       farben[0] = Weiss   (kein Chip / unbekannter Chip)
       farben[1] = Rot
       farben[2] = Gruen
       farben[3] = Blau
       farben[4] = Gelb

   Jeder Reader hat eine eigene Variable farbeReader[0..3]. Sie enthaelt
   den INDEX in farben[] (0..4):
     - Chip liegt auf  -> Farbindex aus der Tabelle CHIPS
     - kein Chip       -> 0 (Weiss)

   Jeder Reader hat ausserdem einen festen Pixel auf der Matrix, der seine
   aktuelle Farbe anzeigt (siehe READER_LED_REIHE / READER_LED_POSITION):
       Reader 1 -> Reihe 15, Position 3
       Reader 2 -> Reihe 15, Position 4
       Reader 3 -> Reihe 15, Position 5
       Reader 4 -> Reihe 15, Position 6

   Abfrage im Code z.B. so:
       if (farbeReader[2] == 1) { ... }            // Reader 3 hat Rot
       setLED(1, 1, farben[farbeReader[2]]);       // Farbe direkt anzeigen

   Die serielle Ausgabe dient nur zur Einrichtung (UIDs auslesen) und
   kann im fertigen Produkt entfernt werden.

   VERKABELUNG (Arduino UNO)
     Gemeinsam an alle 4 Reader:
       SCK   -> Pin 13
       MOSI  -> Pin 11
       MISO  -> Pin 12
       RST   -> Pin 9
       3.3V  -> 3.3V     (NICHT 5V!)
       GND   -> GND
     Einzeln pro Reader:
       SDA/SS Reader 1 -> Pin 10
       SDA/SS Reader 2 -> Pin 8
       SDA/SS Reader 3 -> Pin 7
       SDA/SS Reader 4 -> Pin 6
     LED-Matrix:
       DIN   -> Pin 5
   ========================================================================= */

#include <SPI.h>
#include <MFRC522.h>
#include <Adafruit_GFX.h>
#include <Adafruit_NeoMatrix.h>
#include <Adafruit_NeoPixel.h>

#define PIN 5    // Datenpin der LED-Matrix

// --------------------------- Konfiguration -------------------------------

const byte ANZAHL_READER = 4;

const byte RST_PIN  = 9;   // gemeinsam fuer alle Reader
const byte SS_PIN_1 = 10;
const byte SS_PIN_2 = 8;
const byte SS_PIN_3 = 7;
const byte SS_PIN_4 = 6;

// Laenge der UID in Byte. Die meisten MIFARE-Classic-Chips haben 4 Byte.
// Falls deine Chips 7 Werte ausgeben, hier auf 7 aendern.
const byte UID_LAENGE = 4;

// So viele fehlgeschlagene Abfragen in Folge, bevor "Chip entfernt" gilt.
const byte MAX_FEHLVERSUCHE = 3;

// Pause zwischen zwei kompletten Abfragerunden (ms)
const unsigned int POLL_INTERVALL = 30;

// Wartezeit nach dem Einschalten einer Antenne (ms).
// Falls Chips unzuverlaessig erkannt werden: zuerst erhoehen (z.B. auf 10).
const byte ANTENNEN_WARTEZEIT = 5;

// ---------------------- LED-Zuordnung der Reader -------------------------
// Welcher Pixel der Matrix gehoert zu welchem Reader?
// Das sind die Pixel, die vorher als Testpixel gedient haben.
// Willst du die Anzeige woanders hin verlegen, aenderst du nur diese Zeilen.

const byte READER_LED_REIHE = 15;
const byte READER_LED_POSITION[ANZAHL_READER] = { 3, 4, 5, 6 };

// ------------------------------ Farben -----------------------------------

const byte ANZAHL_FARBEN = 5;

int farben[ANZAHL_FARBEN][3] =
{
  {255, 255, 220},  // 0 Weiss
  {255,   0,   0},  // 1 Rot
  {  0, 255,   0},  // 2 Gruen
  {  0,   0, 255},  // 3 Blau
  {255, 255,   0}   // 4 Gelb
};

// Nur fuer die serielle Ausgabe waehrend der Einrichtung.
// Reihenfolge muss zu farben[] passen!
const char* const FARB_NAMEN[ANZAHL_FARBEN] = {
  "Weiss", "Rot", "Gruen", "Blau", "Gelb"
};

// ------------------------- Chip-Farb-Tabelle -----------------------------
// Hier die UIDs deiner 16 Chips eintragen, dahinter der Farbindex (1..4).
// Schreibweise wahlweise hexadezimal {0xA3, 0x2F, 0x91, 0x7B}
// oder dezimal                       {163,  47,   145,  123}

struct Chip {
  byte uid[UID_LAENGE];
  byte farbe;             // Index in farben[]
};

const Chip CHIPS[] = {
  // ---- Rot (1) ----
  { {0x04, 0x6E, 0x22, 0xBB}, 1 },
  { {0x82, 0x78, 0xC0, 0x80}, 1 },
  { {0x00, 0x00, 0x00, 0x00}, 1 },
  { {0x00, 0x00, 0x00, 0x00}, 1 },
  // ---- Gruen (2) ----
  { {0x9C, 0x6F, 0xA5, 0x80}, 2 },
  { {0x00, 0x00, 0x00, 0x00}, 2 },
  { {0x00, 0x00, 0x00, 0x00}, 2 },
  { {0x00, 0x00, 0x00, 0x00}, 2 },
  // ---- Blau (3) ----
  { {0xD6, 0x33, 0xC7, 0x80}, 3 },
  { {0x00, 0x00, 0x00, 0x00}, 3 },
  { {0x00, 0x00, 0x00, 0x00}, 3 },
  { {0x00, 0x00, 0x00, 0x00}, 3 },
  // ---- Gelb (4) ----
  { {0xB5, 0x0E, 0xC7, 0x80}, 4 },
  { {0x00, 0x00, 0x00, 0x00}, 4 },
  { {0x00, 0x00, 0x00, 0x00}, 4 },
  { {0x00, 0x00, 0x00, 0x00}, 4 }
};

const byte ANZAHL_CHIPS = sizeof(CHIPS) / sizeof(CHIPS[0]);

// --------------------------- Spielvariablen ------------------------------

// Zufaellige Reihenfolge merken (Farbindizes 1..4)
int rndReihenfolge[4];

// Variablen fuer das Blinken
unsigned long letzteZeit = 0;
bool ledAn = false;

// ------------------------------ LED-Matrix -------------------------------
// 16x16 LED-Matrix an PIN
Adafruit_NeoMatrix matrix = Adafruit_NeoMatrix(
  16, 16, PIN,
  NEO_MATRIX_TOP + NEO_MATRIX_LEFT +
  NEO_MATRIX_ROWS + NEO_MATRIX_ZIGZAG,
  NEO_GRB + NEO_KHZ800
);

// ------------------------------ Objekte ----------------------------------

MFRC522 reader[ANZAHL_READER] = {
  MFRC522(SS_PIN_1, RST_PIN),
  MFRC522(SS_PIN_2, RST_PIN),
  MFRC522(SS_PIN_3, RST_PIN),
  MFRC522(SS_PIN_4, RST_PIN)
};

// ------------------------------ Zustand ----------------------------------

// Farbindex pro Reader: farbeReader[0] = Reader 1 (Pin 10) usw.
byte farbeReader[ANZAHL_READER];

byte aktuelleUid[ANZAHL_READER][UID_LAENGE];  // UID des aktuell liegenden Chips
bool chipLiegtAuf[ANZAHL_READER];             // liegt gerade ein Chip drauf?
byte fehlversuche[ANZAHL_READER];             // Zaehler fuer Aussetzer

// --------------------------- Hilfsfunktionen -----------------------------

// Fragt einen Reader ab. Liefert true, wenn ein Chip im Feld liegt,
// und schreibt dessen UID nach uidZiel.
// Es ist immer nur die Antenne des gerade abgefragten Readers an
// (verhindert gegenseitige Stoerung und entlastet den 3.3V-Regler).
bool leseChip(MFRC522 &r, byte* uidZiel) {
  bool gefunden = false;

  r.PCD_AntennaOn();
  delay(ANTENNEN_WARTEZEIT);

  byte atqa[2];
  byte atqaGroesse = sizeof(atqa);

  // WUPA weckt Chips sowohl aus IDLE als auch aus HALT
  MFRC522::StatusCode status = r.PICC_WakeupA(atqa, &atqaGroesse);

  if (status == MFRC522::STATUS_OK || status == MFRC522::STATUS_COLLISION) {
    if (r.PICC_ReadCardSerial()) {
      memcpy(uidZiel, r.uid.uidByte, UID_LAENGE);
      gefunden = true;
    }
    r.PICC_HaltA();
  }

  r.PCD_AntennaOff();
  return gefunden;
}

// Sucht den Farbindex zu einer UID. Unbekannter Chip -> 0 (Weiss)
byte findeFarbe(const byte* uid) {
  for (byte i = 0; i < ANZAHL_CHIPS; i++) {
    if (memcmp(uid, CHIPS[i].uid, UID_LAENGE) == 0) {
      return CHIPS[i].farbe;
    }
  }
  return 0;
}

// Ist ein Tabelleneintrag noch ein unausgefuellter Platzhalter (alles 0)?
bool istPlatzhalter(byte index) {
  for (byte j = 0; j < UID_LAENGE; j++) {
    if (CHIPS[index].uid[j] != 0x00) return false;
  }
  return true;
}

// Serielle Ausgabe eines Readers (nur zur Einrichtung). uid == NULL -> kein Chip
void melde(byte index, const byte* uid) {
  Serial.print(F("Reader "));
  Serial.print(index + 1);
  Serial.print(F(" -> "));

  if (uid != NULL && farbeReader[index] == 0) {
    Serial.print(F("Unbekannter Chip"));
  } else {
    Serial.print(FARB_NAMEN[farbeReader[index]]);
  }

  if (uid != NULL) {
    Serial.print(F("   [dez: "));
    for (byte i = 0; i < UID_LAENGE; i++) {
      Serial.print(uid[i], DEC);
      if (i < UID_LAENGE - 1) Serial.print(' ');
    }
    Serial.print(F(" | hex: "));
    for (byte i = 0; i < UID_LAENGE; i++) {
      if (uid[i] < 0x10) Serial.print('0');
      Serial.print(uid[i], HEX);
      if (i < UID_LAENGE - 1) Serial.print(' ');
    }
    Serial.print(']');
  } else {
    Serial.print(F("   [kein Chip]"));
  }

  Serial.println();
}

// Prueft die Tabelle beim Start auf typische Eintragungsfehler
void pruefeTabelle() {
  Serial.println(F("--- Tabellenpruefung ---"));

  // 1) Noch nicht ausgefuellte Platzhalter
  byte offen = 0;
  for (byte i = 0; i < ANZAHL_CHIPS; i++) {
    if (istPlatzhalter(i)) offen++;
  }
  if (offen > 0) {
    Serial.print(F("HINWEIS: "));
    Serial.print(offen);
    Serial.print(F(" von "));
    Serial.print(ANZAHL_CHIPS);
    Serial.println(F(" Eintraegen noch leer (UID 00 00 00 00)"));
  }

  // 2) Doppelt eingetragene UIDs (Platzhalter ignorieren)
  for (byte i = 0; i < ANZAHL_CHIPS; i++) {
    if (istPlatzhalter(i)) continue;
    for (byte j = i + 1; j < ANZAHL_CHIPS; j++) {
      if (istPlatzhalter(j)) continue;
      if (memcmp(CHIPS[i].uid, CHIPS[j].uid, UID_LAENGE) == 0) {
        Serial.print(F("FEHLER: Zeile "));
        Serial.print(i + 1);
        Serial.print(F(" und Zeile "));
        Serial.print(j + 1);
        Serial.println(F(" haben dieselbe UID!"));
      }
    }
  }

  // 3) Verteilung pro Farbe (Index 1..4) - erwartet werden 4 pro Farbe
  for (byte f = 1; f < ANZAHL_FARBEN; f++) {
    byte anzahl = 0;
    for (byte i = 0; i < ANZAHL_CHIPS; i++) {
      if (CHIPS[i].farbe == f) anzahl++;
    }
    Serial.print(FARB_NAMEN[f]);
    Serial.print(F(": "));
    Serial.print(anzahl);
    Serial.print(F(" Chips"));
    if (anzahl != 4) Serial.print(F("   <-- erwartet waren 4!"));
    Serial.println();
  }

  // 4) Ungueltige Farbindizes
  for (byte i = 0; i < ANZAHL_CHIPS; i++) {
    if (CHIPS[i].farbe < 1 || CHIPS[i].farbe >= ANZAHL_FARBEN) {
      Serial.print(F("FEHLER: Zeile "));
      Serial.print(i + 1);
      Serial.println(F(" hat einen ungueltigen Farbindex (erlaubt: 1..4)!"));
    }
  }

  Serial.println(F("------------------------"));
}

// Eigene Funktion zum Ansprechen einer LED (Reihe/Position ab 1)
void setLED(int reihe, int position, int setfarben[3]) {
  matrix.drawPixel(
    position - 1,   // Spalte
    reihe - 1,      // Reihe
    matrix.Color(setfarben[0], setfarben[1], setfarben[2])
  );
}

//       2    2       255   255   0
//       │    │        │     │     │
//       │    │        │     │     └─ Blau
//       │    │        │     └─────── Grün
//       │    │        └───────────── Rot
//       │    └────────────────────── Position 2
//       └─────────────────────────── Reihe 2

// Setzt den zum Reader gehoerenden Pixel auf dessen aktuelle Farbe.
// farbeReader[index] == 0 bedeutet Weiss, also kein oder unbekannter Chip.
void zeigeReaderFarbe(byte index) {
  setLED(
    READER_LED_REIHE,
    READER_LED_POSITION[index],
    farben[farbeReader[index]]
  );
  matrix.show();
}

// Zufaellige Reihenfolge generieren (Farbindizes 1..4)
void generiereRndReihenfolge() {
  for (int i = 0; i < 4; i++) {
    rndReihenfolge[i] = random(1, 5);   // 1..4 (obere Grenze exklusiv)
  }
}

// Zufaellige Reihenfolge anzeigen                            // AUSKOMMENTIEREN
void zeigeRndReihenfolge() {
  for (int i = 0; i < 4; i++) {
    int farbe = rndReihenfolge[i];   // gespeicherten Farbindex auslesen

    setLED(
      15,             // Reihe 15
      10 + i,         // Position 10 bis 13
      farben[farbe]
    );
  }
  matrix.show();
}

// --------------------------------- Setup ---------------------------------

void setup() {
    // LED-Matrix
  matrix.begin();
  matrix.setBrightness(4);   // Helligkeit einstellen
  matrix.fillScreen(0);      // alle LEDs ausschalten

  // Zufallsgenerator starten
  randomSeed(analogRead(A0));

  // Zufaellige Reihenfolge erzeugen und auf Position 10-13 anzeigen
  generiereRndReihenfolge();
  zeigeRndReihenfolge();

  // Hinweis: Die frueheren Testpixel auf Reihe 15, Position 3-6 werden
  // jetzt als Statusanzeige der vier Reader verwendet. Sie werden weiter
  // unten im Startzustand auf Weiss gesetzt.

  matrix.show();

  // RFID-Sensoren
  Serial.begin(9600);
  while (!Serial) { ; }   // nur bei Boards mit nativem USB relevant

  SPI.begin();

  Serial.println(F("=== Start: 4 RFID-Reader ==="));

  for (byte i = 0; i < ANZAHL_READER; i++) {
    reader[i].PCD_Init();
    delay(10);

    // Diagnose: Erwartung "Firmware Version: 0x92" o.ae. - NICHT 0x00 oder 0xFF.
    Serial.print(F("Reader "));
    Serial.print(i + 1);
    Serial.print(F(": "));
    reader[i].PCD_DumpVersionToSerial();

    // Bei zu geringer Reichweite Empfindlichkeit erhoehen:
    // reader[i].PCD_SetAntennaGain(MFRC522::RxGain_max);

    // Im Betrieb soll immer nur eine Antenne gleichzeitig aktiv sein.
    reader[i].PCD_AntennaOff();

    farbeReader[i]  = 0;      // Weiss
    chipLiegtAuf[i] = false;
    fehlversuche[i] = 0;
  }

  pruefeTabelle();

  Serial.println(F("--- Startzustand ---"));
  for (byte i = 0; i < ANZAHL_READER; i++) {
    melde(i, NULL);
    zeigeReaderFarbe(i);    // alle vier Reader-Pixel auf Weiss
  }
  Serial.println(F("--------------------"));


}

// --------------------------------- Loop ----------------------------------

void loop() {
  byte uid[UID_LAENGE];

  for (byte i = 0; i < ANZAHL_READER; i++) {

    if (leseChip(reader[i], uid)) {
      // --- Ein Chip liegt auf ---
      fehlversuche[i] = 0;

      bool istNeu        = !chipLiegtAuf[i];
      bool istGewechselt = chipLiegtAuf[i] &&
                           memcmp(uid, aktuelleUid[i], UID_LAENGE) != 0;

      if (istNeu || istGewechselt) {
        memcpy(aktuelleUid[i], uid, UID_LAENGE);
        chipLiegtAuf[i] = true;
        farbeReader[i]  = findeFarbe(uid);
        melde(i, uid);
        zeigeReaderFarbe(i);    // Pixel auf die Chipfarbe setzen
      }

    } else {
      // --- Keine Antwort ---
      if (chipLiegtAuf[i]) {
        fehlversuche[i]++;

        if (fehlversuche[i] >= MAX_FEHLVERSUCHE) {
          chipLiegtAuf[i] = false;
          fehlversuche[i] = 0;
          farbeReader[i]  = 0;   // Weiss
          melde(i, NULL);
          zeigeReaderFarbe(i);    // Pixel zurueck auf Weiss
        }
      }
    }
  }

  delay(POLL_INTERVALL);
}
