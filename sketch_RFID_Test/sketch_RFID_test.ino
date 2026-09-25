// RFID-Bibiothek hinzufügen

#include "MFRC522.h"

// Anschlüsse definieren

#define SDA 10

#define RST 9

// RFID-Empfänger benennen, Pins zuordnen

MFRC522 mfrc522(SDA, RST);

void setup()

{
  Serial.println("Setup start");
  Serial.begin(9600);

  SPI.begin();

  // Initialisierung des RFID-Empfängers

  mfrc522.PCD_Init();
  mfrc522.PCD_DumpVersionToSerial();
  Serial.println("Setup fertig");
}

void loop()

{

  String WertDEZ;

  // Wenn keine Karte in Reichweite ist ..

  if (!mfrc522.PICC_IsNewCardPresent())

  {

    // .. wird die Abfrage wiederholt.

    return;

  }

  // Wenn kein RFID-Sender ausgewählt wurde ..

  if (!mfrc522.PICC_ReadCardSerial())

  {

    // .. wird die Abfrage wiederholt.

    return;

  }

  //Serial.println("Karte entdeckt!");

  // Dezimal-Wert in Strings schreiben

  for (byte i = 0; i < mfrc522.uid.size; i++)

  {

    // String zusammenbauen

    WertDEZ = WertDEZ + String(mfrc522.uid.uidByte[i], DEC) + " ";

  }
  //Serial.println("Dezimalwert: " + WertDEZ);

  if (WertDEZ == "130 120 192 128 ") {
    Serial.println("Farbe: Rot");
  } else if (WertDEZ == "214 51 199 128 ") {
    Serial.println("Farbe: Blau");
  } else if (WertDEZ == "156 111 165 128 ") {
    Serial.println("Farbe: Gruen");
  } else if (WertDEZ == "181 14 199 128 ") {
    Serial.println("Farbe:Gelb");
  } else {
    Serial.println("Farbe: unbekannt");
  }
  // Kennung dezimal anzeigen

  //Serial.println("Dezimalwert: " + WertDEZ);


  // kurze Pause, damit nur ein Wert gelesen wird

  delay(1000);

}
