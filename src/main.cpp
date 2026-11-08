#include <Adafruit_NeoPixel.h>
#include <ESP32Servo.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ================= PINOUT DEFINITIONS (ESP32-S3) =================
const int sensorInput[] = {1, 2, 3, 4};
const int sensorCount = 4;
const int threshold = 3600;

#define I2C_SDA         8
#define I2C_SCL         9
#define LCD_I2C_ADDR    0x27
LiquidCrystal_I2C lcd(LCD_I2C_ADDR, 16, 2);

#define LED_PIN         14
const int ledCount = 23;
Adafruit_NeoPixel WS2812B(ledCount, LED_PIN, NEO_GRB + NEO_KHZ800);
const int plotLocation[] = {15, 12, 9, 6};

#define SERVO_ENTRY_PIN 15
#define SERVO_EXIT_PIN  16
Servo servo01;
Servo servo02;

#define BTN_ENTRY_PIN   21
#define BTN_EXIT_PIN    47

#define Print Serial.print
#define Println Serial.println

// ================= TIME MANAGEMENT & STATE =================
bool isLedGuiding = false;
unsigned long lastLedUpdate = 0;
int currentLedPixel = 0;
int targetLedPixel = 0;
const int LED_SPEED = 50;

bool isEntryOpen = false;
unsigned long entryOpenedTime = 0;
const unsigned long ENTRY_DELAY = 3000;

bool isExitOpen = false;
unsigned long exitOpenedTime = 0;
const unsigned long EXIT_DELAY = 3000;

unsigned long lastPrintTime = 0;

bool isFlashingRed = false;
unsigned long lastFlashUpdate = 0;
int flashCount = 0;
bool flashState = false;
const int FLASH_INTERVAL = 200;
const int MAX_FLASHES = 6;

int lastAvailableSlots = -1;
bool isShowingMessage = false;
unsigned long lcdMessageTime = 0;
const unsigned long LCD_MSG_DURATION = 3000;

bool lastEntryBtnState = HIGH;
bool lastExitBtnState = HIGH;

// Biến quản lý định kỳ kiểm tra sức khỏe thiết bị
bool isLcdHealthy = false;
unsigned long lastHealthCheckTime = 0;
const unsigned long HEALTH_CHECK_INTERVAL = 3000; // Kiểm tra bus I2C mỗi 3 giây

// ================= RE-INIT & HEALTH CHECK FUNCTIONS =================

// 1. Kiểm tra và tái khởi tạo bus I2C & LCD nếu mất kết nối
bool checkAndReinitLCD() {
  Wire.beginTransmission(LCD_I2C_ADDR);
  byte error = Wire.endTransmission();

  if (error != 0) {
    // 0: Thành công, các mã khác: NACK hoặc lỗi bus
    Println("[WARN] LCD / I2C Bus bi mat ket noi hoac phan hoi loi. Dang thu re-init...");
    isLcdHealthy = false;
    
    // Reset phần cứng bus I2C trên ESP32
    Wire.end();
    delay(20);
    Wire.begin(I2C_SDA, I2C_SCL);
    
    // Thử kết nối lại lần nữa
    Wire.beginTransmission(LCD_I2C_ADDR);
    if (Wire.endTransmission() == 0) {
      lcd.init();
      lcd.backlight();
      lastAvailableSlots = -1; // Buộc màn hình vẽ lại ngay lập tức
      isLcdHealthy = true;
      Println("[OK] LCD da duoc khoi phuc thanh cong!");
      return true;
    }
    return false;
  }
  
  if (!isLcdHealthy) {
    // Trường hợp trước đó lỗi nhưng giờ thiết bị đã phản hồi
    lcd.init();
    lcd.backlight();
    lastAvailableSlots = -1;
    isLcdHealthy = true;
    Println("[OK] LCD khoi dong lai sau su co!");
  }
  return true;
}

// 2. Tái cấu hình lại Servo khi phát hiện trạng thái detach hoặc cần force reset
void reinitServo(Servo &servo, int pin, int defaultAngle) {
  Print("[INFO] Re-initializing Servo tai Pin ");
  Println(pin);
  
  if (servo.attached()) {
    servo.detach();
  }
  servo.setPeriodHertz(50);
  servo.attach(pin, 500, 2400);
  servo.write(defaultAngle);
}

// 3. Khởi tạo lại toàn bộ Servo nếu timer hoặc GPIO bị treo
void reinitAllServos() {
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  reinitServo(servo01, SERVO_ENTRY_PIN, 0);
  reinitServo(servo02, SERVO_EXIT_PIN, 0);
  isEntryOpen = false;
  isExitOpen = false;
}

// 4. Khởi tạo lại WS2812B LED (phòng trường hợp bộ đệm RMT/SPI bị treo)
void reinitLEDStrip() {
  Println("[INFO] Re-initializing WS2812B Strip...");
  WS2812B.begin();
  WS2812B.setBrightness(255);
  WS2812B.clear();
  WS2812B.show();
}

// ================= LOGIC ĐIỀU KHIỂN =================

void initGPIO() {
  for (int i = 0; i < sensorCount; i++) {
    pinMode(sensorInput[i], INPUT);
  }
  pinMode(BTN_ENTRY_PIN, INPUT_PULLUP);
  pinMode(BTN_EXIT_PIN, INPUT_PULLUP);
}

bool isCar(int n) {
  if (n >= sensorCount || n < 0) return true;
  return analogRead(sensorInput[n]) < threshold;
}

int findEmptySlot() {
  for (int i = 0; i < sensorCount; i++) {
    if (!isCar(i)) return i;
  }
  return -1;
}

void updateLedAnimation() {
  if (isLedGuiding) {
    if (millis() - lastLedUpdate >= LED_SPEED) {
      lastLedUpdate = millis();

      if (currentLedPixel > 0) {
        WS2812B.setPixelColor(currentLedPixel - 1, WS2812B.Color(0, 0, 0));
      }

      if (currentLedPixel > targetLedPixel) {
        isLedGuiding = false;
        WS2812B.show();
        return;
      }

      WS2812B.setPixelColor(currentLedPixel, WS2812B.Color(0, 255, 0));
      WS2812B.show();
      currentLedPixel++;
    }
  }
}

void updateFullWarning() {
  if (isFlashingRed) {
    if (millis() - lastFlashUpdate >= FLASH_INTERVAL) {
      lastFlashUpdate = millis();
      flashState = !flashState;

      if (flashState) {
        WS2812B.fill(WS2812B.Color(255, 0, 0));
      } else {
        WS2812B.clear();
      }
      WS2812B.show();
      flashCount++;

      if (flashCount >= MAX_FLASHES) {
        isFlashingRed = false;
        flashCount = 0;
        WS2812B.clear();
        WS2812B.show();
      }
    }
  }
}

int getAvailableSlots() {
  int count = 0;
  for (int i = 0; i < sensorCount; i++) {
    if (!isCar(i)) count++;
  }
  return count;
}

void updateLCDDisplay() {
  if (!isLcdHealthy) return; // Bỏ qua nếu màn hình đang ngắt kết nối để tránh lag bus

  if (!isShowingMessage) {
    int currentSlots = getAvailableSlots();
    if (currentSlots != lastAvailableSlots) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print(" SMART PARKING  ");
      lcd.setCursor(0, 1);
      lcd.print(" Available: ");
      lcd.print(currentSlots);
      lcd.print("/");
      lcd.print(sensorCount);
      lastAvailableSlots = currentSlots;
    }
  } else {
    if (millis() - lcdMessageTime >= LCD_MSG_DURATION) {
      isShowingMessage = false;
      lastAvailableSlots = -1;
    }
  }
}

// ================= SETUP & LOOP =================
void setup() {
  Serial.begin(115200);

  Wire.begin(I2C_SDA, I2C_SCL);
  checkAndReinitLCD();

  initGPIO();
  reinitLEDStrip();
  reinitAllServos();

  Println("======================================");
  Println("ESP32-S3 Parking Ready (Self-Healing Active)");
  Println("======================================");
}

void loop() {
  // 1. ĐỊNH KỲ KIỂM TRA SỨC KHỎE LINH KIỆN (NON-BLOCKING)
  if (millis() - lastHealthCheckTime >= HEALTH_CHECK_INTERVAL) {
    lastHealthCheckTime = millis();
    checkAndReinitLCD();
    
    // Kiểm tra trạng thái attach của servo đề phòng rớt PWM
    if (!servo01.attached()) reinitServo(servo01, SERVO_ENTRY_PIN, 0);
    if (!servo02.attached()) reinitServo(servo02, SERVO_EXIT_PIN, 0);
  }

  // 2. CẬP NHẬT HIỆU ỨNG VÀ MÀN HÌNH
  updateLedAnimation();
  updateFullWarning();
  updateLCDDisplay();

  // 3. TỰ ĐỘNG ĐÓNG CỔNG
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

  // 4. NÚT NHẤN CỔNG VÀO
  bool currentEntryBtnState = digitalRead(BTN_ENTRY_PIN);
  if (lastEntryBtnState == HIGH && currentEntryBtnState == LOW) {
    if (!isEntryOpen) {
      int emptySlot = findEmptySlot();
      if (emptySlot != -1) {
        Println("=> Entry button triggered.");
        if (isLcdHealthy) {
          lcd.clear();
          lcd.setCursor(0, 0); lcd.print(" Welcome!");
          lcd.setCursor(0, 1); lcd.print(" Go to slot: "); lcd.print(emptySlot);
          isShowingMessage = true;
          lcdMessageTime = millis();
        }

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
        if (isLcdHealthy) {
          lcd.clear();
          lcd.setCursor(0, 0); lcd.print(" Parking Full!");
          lcd.setCursor(0, 1); lcd.print(" Sorry :(");
          isShowingMessage = true;
          lcdMessageTime = millis();
        }

        if (!isFlashingRed && !isLedGuiding) {
          isFlashingRed = true;
          flashCount = 0;
          flashState = false;
          lastFlashUpdate = millis();
        }
      }
    }
  }
  lastEntryBtnState = currentEntryBtnState;

  // 5. NÚT NHẤN CỔNG RA
  bool currentExitBtnState = digitalRead(BTN_EXIT_PIN);
  if (lastExitBtnState == HIGH && currentExitBtnState == LOW) {
    if (!isExitOpen) {
      Println("=> Exit button triggered.");
      if (isLcdHealthy) {
        lcd.clear();
        lcd.setCursor(0, 0); lcd.print(" Goodbye!");
        lcd.setCursor(0, 1); lcd.print(" Have a nice day");
        isShowingMessage = true;
        lcdMessageTime = millis();
      }

      servo02.write(90);
      isExitOpen = true;
      exitOpenedTime = millis();
    }
  }
  lastExitBtnState = currentExitBtnState;

  // 6. SERIAL MONITOR DEBUG
  if (millis() - lastPrintTime >= 500) {
    lastPrintTime = millis();
    Print("Slot: [");
    for (int i = 0; i < sensorCount; i++) {
      Print(isCar(i) ? "X" : "O");
      if (i < sensorCount - 1) Print(" | ");
    }
    Print("] | LCD: ");
    Println(isLcdHealthy ? "OK" : "DISCONNECTED");
  }

  delay(10);
}