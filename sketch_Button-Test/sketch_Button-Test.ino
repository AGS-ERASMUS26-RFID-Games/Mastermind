#define BUTTON_PIN 2
void setup() {
  // put your setup code here, to run once:
  Serial.begin(9600);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

}

void loop() {
  // put your main code here, to run repeatedly:
  if (digitalRead(BUTTON_PIN) == LOW) {
    Serial.println("Knopf gedrueckt!");
    delay(200); // einfache Entprellung
  }
}
