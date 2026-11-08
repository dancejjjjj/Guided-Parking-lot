#include <Adafruit_NeoPixel.h>

#define LED_PIN         18     
#define NUM_LEDS        20     
#define BRIGHTNESS      100    

#define SEGMENT_LENGTH  5      
#define SPEED_DELAY     10     
#define HUE_STEP        2000   

Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

uint16_t baseHue = 0;        
void rainbowSegments() {
  for (int i = 0; i < NUM_LEDS; i++) {

    int segmentIndex = i / SEGMENT_LENGTH;


    uint16_t segmentHue = baseHue + (segmentIndex * HUE_STEP);

   
    uint32_t color = strip.ColorHSV(segmentHue, 255, 255);

    
    strip.setPixelColor(i, strip.gamma32(color));
  }


  baseHue += 256;
}

void setup() {
  strip.begin();
  strip.setBrightness(BRIGHTNESS);
  strip.show();                
}

void loop() {
  rainbowSegments();
  strip.show();
  delay(SPEED_DELAY);
}
