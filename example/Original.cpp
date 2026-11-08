// port from my Wokwi code for Arduino to ESP32 
// https://wokwi.com/projects/472470733505983489

#include <SPI.h>
#include <MFRC522.h>
#include <Adafruit_NeoPixel.h>
#include <Servo.h>

#include <Wire.h>
#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);

#define Print Serial.print
#define Println Serial.println

// --- Sensor Configuration ---
const int threshold = 900;
const int sensorCount = 4;
const int sensorInput[] = {A0, A1, A2, A3};

// --- LED Configuration ---
const int ledPin = 4;
const int ledCount = 23;
Adafruit_NeoPixel WS2812B(ledCount, ledPin, NEO_GRB + NEO_KHZ800);
const int plotLocation[] = {20, 15, 10, 4}; // LED index for 4 parking slots

// --- Servo Configuration ---
const int servoPin01 = 5;
Servo servo01;
const int servoPin02 = 6;
Servo servo02;

// --- RFID Configuration ---
#define RST_PIN    9   
#define SS_1_PIN   3   // Entry card reader
#define SS_2_PIN   2   // Exit card reader

MFRC522 rfid_entry(SS_1_PIN, RST_PIN);
MFRC522 rfid_exit(SS_2_PIN, RST_PIN);

// ================= TIME MANAGEMENT VARIABLES (NON-BLOCKING) =================

// 1. Variables for green guiding LED
bool isLedGuiding = false;
unsigned long lastLedUpdate = 0;
int currentLedPixel = 0;
int targetLedPixel = 0;
const int LED_SPEED = 50; // LED chase speed (ms)

// 2. Variables for entry gate servo
bool isEntryOpen = false;
unsigned long entryOpenedTime = 0;
const unsigned long ENTRY_DELAY = 3000; // Entry gate open duration (ms)

// 3. Variables for exit gate servo
bool isExitOpen = false;
unsigned long exitOpenedTime = 0;
const unsigned long EXIT_DELAY = 3000; // Exit gate open duration (ms)

// 4. Variables for Serial Monitor
unsigned long lastPrintTime = 0;

// 5. Variables for flashing red warning (parking full)
bool isFlashingRed = false;
unsigned long lastFlashUpdate = 0;
int flashCount = 0;
bool flashState = false;
const int FLASH_INTERVAL = 200; // Blink interval (200ms)
const int MAX_FLASHES = 6;      // Blink 3 times (3 ON + 3 OFF = 6 state changes)

// 6. Variables for LCD display management
int lastAvailableSlots = -1; // Previous available slots count (-1 forces first update)
bool isShowingMessage = false; // State indicating whether LCD is busy showing a greeting message
unsigned long lcdMessageTime = 0; 
const unsigned long LCD_MSG_DURATION = 3000; // Message display duration (3 seconds)

// =============================================================================

void initGPIO() {
  for (auto pin : sensorInput) {
    pinMode(pin, INPUT);
  }
}

// Check if slot n is occupied by a vehicle
bool isCar(int n) {
  if (n >= sensorCount || n < 0) return true; 
  return analogRead(sensorInput[n]) < threshold;
}

// Find the first available empty slot
int findEmptySlot() {
  for (int i = 0; i < sensorCount; i++) {
    if (!isCar(i)) return i; 
  }
  return -1; // -1 indicates the parking lot is full
}

// Function to update green guiding LED animation (runs continuously in loop)
void updateLedAnimation() {
  if (isLedGuiding) {
    if (millis() - lastLedUpdate >= LED_SPEED) {
      lastLedUpdate = millis();
      
      // Turn off previous LED pixel
      if (currentLedPixel > 0) {
        WS2812B.setPixelColor(currentLedPixel - 1, WS2812B.Color(0, 0, 0));
      }
      
      // Stop animation once target is reached
      if (currentLedPixel > targetLedPixel) {
        isLedGuiding = false;
        WS2812B.show();
        return;
      }
      
      // Turn on current LED pixel
      WS2812B.setPixelColor(currentLedPixel, WS2812B.Color(0, 255, 0));
      WS2812B.show();
      
      currentLedPixel++;
    }
  }
}

// Function to update flashing red warning (runs continuously in loop)
void updateFullWarning() {
  if (isFlashingRed) {
    if (millis() - lastFlashUpdate >= FLASH_INTERVAL) {
      lastFlashUpdate = millis();
      flashState = !flashState; // Toggle state (ON <-> OFF)
      
      if (flashState) {
        WS2812B.fill(WS2812B.Color(255, 0, 0)); // Turn all LEDs red
      } else {
        WS2812B.clear(); // Turn off all LEDs
      }
      WS2812B.show();
      
      flashCount++;
      
      // Stop once target flash count is reached
      if (flashCount >= MAX_FLASHES) {
        isFlashingRed = false;
        flashCount = 0;
        WS2812B.clear();
        WS2812B.show();
      }
    }
  }
}

// Function to count current available slots
int getAvailableSlots() {
  int count = 0;
  for (int i = 0; i < sensorCount; i++) {
    if (!isCar(i)) count++;
  }
  return count;
}

// Function to manage LCD display updates
void updateLCDDisplay() {
  // If display is NOT busy showing a "Welcome/Goodbye" message
  if (!isShowingMessage) {
    int currentSlots = getAvailableSlots();
    
    // Only refresh display IF the number of available slots has changed
    if (currentSlots != lastAvailableSlots) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print(" SMART PARKING  ");
      lcd.setCursor(0, 1);
      lcd.print(" Available: ");
      lcd.print(currentSlots);
      lcd.print("/");
      lcd.print(sensorCount);
      
      lastAvailableSlots = currentSlots; // Update cached count
    }
  } 
  // If displaying greeting message, check if duration has elapsed
  else {
    if (millis() - lcdMessageTime >= LCD_MSG_DURATION) {
      isShowingMessage = false; // Clear greeting mode
      lastAvailableSlots = -1;  // Force immediate redraw of slot availability
    }
  }
}

void setup() {
  Serial.begin(9600);
  SPI.begin();
  
  // Initialize both RFID modules
  rfid_entry.PCD_Init();
  rfid_exit.PCD_Init();

  initGPIO();
  
  // Initialize LED strip
  WS2812B.begin();
  WS2812B.setBrightness(255);
  WS2812B.clear();
  WS2812B.show();

  // Initialize servos to 0 degrees
  servo01.attach(servoPin01);
  servo02.attach(servoPin02);
  servo01.write(0);
  servo02.write(0);

  // ---> LCD INITIALIZATION SECTION <---
  lcd.init();         // Initialize LCD
  lcd.backlight();    // Turn on backlight
  lcd.setCursor(0, 0);
  lcd.print("System Ready!"); 
  // ------------------------------------
  
  Println("======================================");
  Println("Non-Blocking Parking System Ready!");
  Println("======================================");
}

void loop() {
  // 1. UPDATE ANIMATIONS AND DISPLAY CONTINUOUSLY
  updateLedAnimation();
  updateFullWarning(); 
  updateLCDDisplay(); // <--- Automatic LCD refresh function

  // 2. AUTO-CLOSE SERVOS
  if (isEntryOpen && (millis() - entryOpenedTime >= ENTRY_DELAY)) {
    servo01.write(0);
    isEntryOpen = false;
    Println("-> ENTRY gate closed.");
  }

  if (isExitOpen && (millis() - exitOpenedTime >= EXIT_DELAY)) {
    servo02.write(0);
    isExitOpen = false;
    Println("-> EXIT gate closed.");
  }

  // 3. CHECK ENTRY RFID CARD
  if (rfid_entry.PICC_IsNewCardPresent() && rfid_entry.PICC_ReadCardSerial()) {
    if (!isEntryOpen) {
      int emptySlot = findEmptySlot();
      if (emptySlot != -1) {
        Println("=> Entry card verified.");
        
        // --- Display welcome message on LCD ---
        lcd.clear();
        lcd.setCursor(0, 0); lcd.print(" Welcome!");
        lcd.setCursor(0, 1); lcd.print(" Go to slot: "); lcd.print(emptySlot);
        isShowingMessage = true;
        lcdMessageTime = millis();
        // --------------------------------------

        servo01.write(90);
        isEntryOpen = true;
        entryOpenedTime = millis();
        
        isFlashingRed = false; 
        WS2812B.clear();
        
        isLedGuiding = true;
        currentLedPixel = 0;
        targetLedPixel = plotLocation[emptySlot];
        lastLedUpdate = millis();
      } else {
        Println("! Parking FULL !");
        
        // --- Display parking full warning on LCD ---
        lcd.clear();
        lcd.setCursor(0, 0); lcd.print(" Parking Full!");
        lcd.setCursor(0, 1); lcd.print(" Sorry :(");
        isShowingMessage = true;
        lcdMessageTime = millis();
        // ------------------------------------------

        if (!isFlashingRed && !isLedGuiding) {
          isFlashingRed = true;
          flashCount = 0;
          flashState = false;
          lastFlashUpdate = millis();
        }
      }
    }
    rfid_entry.PICC_HaltA(); 
  }

  // 4. CHECK EXIT RFID CARD
  if (rfid_exit.PICC_IsNewCardPresent() && rfid_exit.PICC_ReadCardSerial()) {
    if (!isExitOpen) {
      Println("=> Exit card verified.");
      
      // --- Display goodbye message on LCD ---
      lcd.clear();
      lcd.setCursor(0, 0); lcd.print(" Goodbye!");
      lcd.setCursor(0, 1); lcd.print(" Have a nice day");
      isShowingMessage = true;
      lcdMessageTime = millis();
      // --------------------------------------
      
      servo02.write(90);
      isExitOpen = true;
      exitOpenedTime = millis();
    }
    rfid_exit.PICC_HaltA(); 
  }

  // 5. PRINT STATUS TO SERIAL MONITOR
  if (millis() - lastPrintTime >= 500) {
    lastPrintTime = millis();
    Print("Slot: [");
    for (int i = 0; i < sensorCount; i++) {
      Print(isCar(i) ? "X" : "O"); 
      if (i < sensorCount - 1) Print(" | ");
    }
    Println("]");
  }
  delay(10);
}