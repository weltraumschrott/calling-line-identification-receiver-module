//#include <AltSoftSerial.h> // RX = 8, TX = 9; https://www.pjrc.com/teensy/td_libs_AltSoftSerial.html
#include <SoftwareSerial.h>
#include <LiquidCrystal.h>


// pin definitions
LiquidCrystal lcd(2, 3, 4, 5, 6, 7);

const int backlightPin = 10;
const int led1Pin = 11; // currently not in use
const int led2Pin = 12; // currently not in use
const int buttonPin = 13;


// serial communication
//AltSoftSerial ht9032Serial;
SoftwareSerial ht9032Serial(8, 9);


// parsing incoming serial data, formatting parameters
/*
  parameter: date and time
  type indicator: 0x01
  expected length: 8

  parameter: calling line identity
  type indicator: 0x02
  expected length: usually max. 20

  parameter: reason for absence of calling line identity
  type indicator: 0x04
  expected length: 1

  parameter: calling party name
  type indicator: 0x07
  expected length: max. 50

  parameter: reason for absence of calling party name
  type indicator: 0x08
  expected length: 1
*/

char dateAndTime[8];
char callingLineIdentity[21]; // one element added for null termination (to read array as a string)
char reasonForAbsenceOfCallingLineIdentity = 0;
char callingPartyName[51]; // one element added for null termination (to read array as a string)
char reasonForAbsenceOfCallingPartyName = 0;
int callingLineIdentityLength = 0;
int callingPartyNameLength = 0;
char dateAndTimeString[12] = "00-00 00:00"; // one element added for null termination (to read array as a string)

int ht9032ReadState = 0;

int incomingByte = 0;

int markLength = 0; // number of bytes in mark indicating upcoming message
int postMarkLength = 0; // number of bytes directly after mark
int messageLength = 0; // number of bytes in message body (parameter headers and bodies)
int parameterLength = 0; // number of bytes in parameter body
int messageLengthCounter = 0;
int parameterLengthCounter = 0;


// UI

// general timing
unsigned long currentMillis = 0;

// button debouncing
int buttonState = LOW;
int previousButtonState = LOW;
unsigned long previousDebounceMillis = 0;
int debounceTime = 50;

// LCD event
bool lcdEvent = false;
bool newLcdEvent = false;
unsigned long lcdEventMillis = 0;
const int lcdEventTime = 10000;

// toggle to and from calling party name
unsigned long previousCallingPartyNameToggleMillis = 0;
const int callingPartyNameToggleTime = 2000;
bool callingPartyNameToggleState = false;


void setup() {
  pinMode(backlightPin, OUTPUT);
//  pinMode(led1Pin, OUTPUT);
//  pinMode(led2Pin, OUTPUT);
  pinMode(buttonPin, INPUT);

  Serial.begin(115200);
  delay(10);

  ht9032Serial.begin(1200);
  delay(10);

  lcd.begin(16, 2);
  lcd.clear();
  digitalWrite(backlightPin, HIGH);
  lcd.setCursor(0, 1);
  lcd.print("Starting...");
  Serial.println("Starting...");
  delay(1000);
  digitalWrite(backlightPin, LOW);
  lcd.clear();
}


void loop() {
  currentMillis = millis();

  lcdEventHandler();

  ht9032Read();

  // check button state (incl. debouncing)
  int buttonStateReading = digitalRead(buttonPin);

  if (buttonStateReading != previousButtonState) {
    previousDebounceMillis = currentMillis;
  }

  if ((currentMillis - previousDebounceMillis) > debounceTime) {
    if (buttonStateReading != buttonState) {
      buttonState = buttonStateReading;

      if (buttonState == HIGH) {
        newLcdEvent = true;
      }
    }
  }

  previousButtonState = buttonStateReading;

  // check state of parameter parser
  if (ht9032ReadState == 13) {
    newLcdEvent = true;

    // reset state
    ht9032ReadState = 0;

    // format date and time
    dateAndTimeString[0] = dateAndTime[0];
    dateAndTimeString[1] = dateAndTime[1];
    dateAndTimeString[2] = '-';
    dateAndTimeString[3] = dateAndTime[2];
    dateAndTimeString[4] = dateAndTime[3];
    dateAndTimeString[5] = ' ';
    dateAndTimeString[6] = dateAndTime[4];
    dateAndTimeString[7] = dateAndTime[5];
    dateAndTimeString[8] = ':';
    dateAndTimeString[9] = dateAndTime[6];
    dateAndTimeString[10] = dateAndTime[7];

    serialPrint();
  }
}


void lcdEventHandler() {
  if (newLcdEvent == true) {
    lcdEventMillis = currentMillis;
    newLcdEvent = false;
    lcdEvent = true;

    // prepare LCD and print date and time
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(dateAndTimeString);

    // activate LCD backlight
    digitalWrite(backlightPin, HIGH);
  }

  if (lcdEvent == true) {
    // check for calling party name
    if (callingPartyNameLength > 0) {
      // toggle between calling line identity and calling party name
      if (callingPartyNameToggleState == false) {
        if (currentMillis - previousCallingPartyNameToggleMillis >= callingPartyNameToggleTime) {
          lcd.setCursor(0, 1);
          lcd.print("                "); // to clear only second line
          lcd.setCursor(0, 1);
          lcd.print(callingPartyName);
          callingPartyNameToggleState = true;
          previousCallingPartyNameToggleMillis = currentMillis;
        }
      }
      else {
        if (currentMillis - previousCallingPartyNameToggleMillis >= callingPartyNameToggleTime) {
          lcd.setCursor(0, 1);
          lcd.print("                "); // to clear only second line
          lcd.setCursor(0, 1);
          if (callingLineIdentityLength > 0) {
            lcd.print(callingLineIdentity);
          }
          else {
            if (reasonForAbsenceOfCallingLineIdentity == 0x4F) {
              lcd.print("[unavailable]");
            }
            else if (reasonForAbsenceOfCallingLineIdentity == 0x50) {
              lcd.print("[private]");
            }
          }
          callingPartyNameToggleState = false;
          previousCallingPartyNameToggleMillis = currentMillis;
        }
      }
    }
    else {
      lcd.setCursor(0, 1);
      if (callingLineIdentityLength > 0) {
        lcd.print(callingLineIdentity);
      }
      else {
        if (reasonForAbsenceOfCallingLineIdentity == 0x4F) {
          lcd.print("[unavailable]");
        }
        else if (reasonForAbsenceOfCallingLineIdentity == 0x50) {
          lcd.print("[private]");
        }
      }
    }

    // check if time has elapsed
    if (currentMillis - lcdEventMillis >= lcdEventTime) {
      lcdEvent = false;

      // prepare LCD for next event
      lcd.clear();

      // deactivate LCD backlight
      digitalWrite(backlightPin, LOW);
    }
  }
}


void serialPrint() {
  Serial.println();
  Serial.println("-> state 13");
  Serial.print("date and time: ");
  Serial.println(dateAndTimeString);
  Serial.print("calling line identity: ");
  Serial.println(callingLineIdentity);
  Serial.print("reason for absence of calling line identity: ");
  Serial.print(reasonForAbsenceOfCallingLineIdentity);
  if (reasonForAbsenceOfCallingLineIdentity > 0) {
    if (reasonForAbsenceOfCallingLineIdentity == 0x4F) {
      Serial.println(" [unavailable]");
    }
    else if (reasonForAbsenceOfCallingLineIdentity == 0x50) {
      Serial.println(" [private]");
    }
  }
  else {
    Serial.println();
  }
  Serial.print("calling party name: ");
  Serial.println(callingPartyName);
  Serial.print("reason for absence of calling party name: ");
  Serial.print(reasonForAbsenceOfCallingPartyName);
  if (reasonForAbsenceOfCallingPartyName > 0) {
    if (reasonForAbsenceOfCallingPartyName == 0x4F) {
      Serial.println(" [unavailable]");
    }
    else if (reasonForAbsenceOfCallingPartyName == 0x50) {
      Serial.println(" [private]");
    }
  }
  else {
    Serial.println();
  }
}


void ht9032Read() {
  if (ht9032Serial.available() > 0) {
    incomingByte = ht9032Serial.read(); // incoming serial data

    Serial.println();
    Serial.print("-> state ");
    Serial.println(ht9032ReadState);
    Serial.print("incoming serial data: ");
    Serial.println(String(incomingByte, HEX));

    Serial.print("message length counter: ");
    Serial.print(messageLengthCounter);
    Serial.print("/");
    Serial.println(messageLength);

    Serial.print("parameter length counter: ");
    Serial.print(parameterLengthCounter);
    Serial.print("/");
    Serial.println(parameterLength);

    switch (ht9032ReadState) {
      case 0: // detecting message
        if (incomingByte == 0x55) { // 0x55 (U) ≙ mark indicating upcoming message
          markLength++;
          postMarkLength = 0;
        }
        else if (incomingByte == 0x80) { // 0x80 ≙ message type: MDMF packet (call set-up message)
          if (markLength >= 10) { // check if mark was definitive
            ht9032ReadState = 1;

            // reset variables
            memset(dateAndTime, 0, sizeof(dateAndTime));
            memset(callingLineIdentity, 0, sizeof(callingLineIdentity));
            reasonForAbsenceOfCallingLineIdentity = 0;
            memset(callingPartyName, 0, sizeof(callingPartyName));
            reasonForAbsenceOfCallingPartyName = 0;

            callingLineIdentityLength = 0;
            callingPartyNameLength = 0;

            messageLengthCounter = 0;

            markLength = 0;
            postMarkLength = 0;
          }
        }
        else if (markLength > 0) {
          postMarkLength++;
          if (postMarkLength > 2) { // check if distance to mark is too big to be just a minor inconsistency
            markLength = 0; // restart counting mark length
          }
        }
        break;

      case 1: // message length: MDMF packet (call set-up message)
        messageLength = incomingByte;
        ht9032ReadState = 2;
        break;

      case 2: // parameter type
        messageLengthCounter++; // increase counter, even if the incoming byte will not be recognized
        
        parameterLength = 0;
        parameterLengthCounter = 0;

        if (messageLengthCounter >= messageLength) { // >= in anticipation of incorrectly statet lengths of message or parameter(s)
          ht9032ReadState = 13;
          messageLength = 0;
          messageLengthCounter = 0;
        }
        else if (incomingByte == 0x01) { // 0x01 ≙ parameter type: date and time
          ht9032ReadState = 3;
        }
        else if (incomingByte == 0x02) { // 0x02 ≙ parameter type: calling line identity (the calling telephone number)
          ht9032ReadState = 5;
        }
        else if (incomingByte == 0x04) { // 0x04 ≙ parameter type: reason for absence of calling line identity
          ht9032ReadState = 7;
        }
        else if (incomingByte == 0x07) { // 0x07 ≙ parameter type: calling party name
          ht9032ReadState = 9;
        }
        else if (incomingByte == 0x08) { // 0x08 ≙ parameter type: reason for absence of calling party name
          ht9032ReadState = 11;
        }
        break;

      case 3: // parameter length: date and time
        messageLengthCounter++;
        parameterLength = incomingByte;
        ht9032ReadState = 4;
        break;

      case 4: // parameter: date and time
        messageLengthCounter++;
        if (parameterLengthCounter < sizeof(dateAndTime)) { // limit counter to array size
          dateAndTime[parameterLengthCounter] = incomingByte;
        }
        parameterLengthCounter++;
        if (parameterLengthCounter == parameterLength) {
          ht9032ReadState = 2;
        }
        break;

      case 5: // parameter length: calling line identity (the calling telephone number)
        messageLengthCounter++;
        callingLineIdentityLength = incomingByte;
        parameterLength = incomingByte;
        ht9032ReadState = 6;
        break;

      case 6: // parameter: calling line identity (the calling telephone number)
        messageLengthCounter++;
        if (parameterLengthCounter < sizeof(callingLineIdentity) - 1) { // limit counter to usable array size (keeping one element free for the null character)
          callingLineIdentity[parameterLengthCounter] = incomingByte;
        }
        parameterLengthCounter++;
        if (parameterLengthCounter == callingLineIdentityLength) {
          ht9032ReadState = 2;
        }
        break;

      case 7: // parameter length: reason for absence of calling line identity
        messageLengthCounter++;
        parameterLength = incomingByte;
        ht9032ReadState = 8;
        break;

      case 8: // parameter: reason for absence of calling line identity
        messageLengthCounter++;
        reasonForAbsenceOfCallingLineIdentity = incomingByte; // if parameter length is unexpectedly high (>1) use the last byte
        parameterLengthCounter++;
        if (parameterLengthCounter == parameterLength) {
          ht9032ReadState = 2;
        }
        break;

      case 9: // parameter length: calling party name
        messageLengthCounter++;
        callingPartyNameLength = incomingByte;
        parameterLength = incomingByte;
        ht9032ReadState = 10;
        break;

      case 10: // parameter: calling party name
        messageLengthCounter++;
        if (parameterLengthCounter < sizeof(callingPartyName) - 1) { // limit counter to usable array size (keeping one element free for the null character)
          callingPartyName[parameterLengthCounter] = incomingByte;
        }
        parameterLengthCounter++;
        if (parameterLengthCounter == callingPartyNameLength) {
          ht9032ReadState = 2;
        }
        break;

      case 11: // parameter length: reason for absence of calling party name
        messageLengthCounter++;
        parameterLength = incomingByte;
        ht9032ReadState = 12;
        break;

      case 12: // parameter: reason for absence of calling party name
        messageLengthCounter++;
        reasonForAbsenceOfCallingPartyName = incomingByte; // if parameter length is unexpectedly high (>1) use the last byte
        parameterLengthCounter++;
        if (parameterLengthCounter == parameterLength) {
          ht9032ReadState = 2;
        }
        break;

      default:
        break;
    }
  }
}
