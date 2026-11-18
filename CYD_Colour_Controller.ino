/*
  CYD_Colour_Controller.ino

  Hardware:
    - Standard ESP32-2432S028R CYD
    - ILI9341 display, portrait orientation
    - XPT2046 resistive touchscreen
    - WS2812 / NeoPixel LED strip

  Arduino libraries:
    - TFT_eSPI by Bodmer
    - XPT2046_Touchscreen by Paul Stoffregen
    - Adafruit NeoPixel by Adafruit

  IMPORTANT:
    1. Configure TFT_eSPI/User_Setup.h for the CYD display pins.
    2. Touch calibration values below are provisional. Calibrate your panel.
    3. Use a suitable external 5 V supply for the WS2812 strip, with
       common ground. Use a 3.3 V-to-5 V logic buffer where appropriate.
*/

#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <Adafruit_NeoPixel.h>

// ============================================================================
// 1. USER CONFIGURATION
// ============================================================================

// Display rotation:
// TFT_eSPI rotation 0 is normally portrait (240 x 320) on the standard CYD.
#define DISPLAY_ROTATION       0

// Touchscreen pins on the standard CYD.
#define TOUCH_CS               33
#define TOUCH_IRQ              36
#define TOUCH_SCK              25
#define TOUCH_MISO             39
#define TOUCH_MOSI             32

// WS2812 configuration. Change these to suit your installation.
#define LED_PIN                22
#define NUM_LEDS               12
#define LED_BRIGHTNESS         64
#define LED_PIXEL_TYPE         (NEO_GRB + NEO_KHZ800)

// Touch calibration.
// These are STARTING VALUES ONLY. Use the Serial Monitor diagnostics
// and calibrate the four corners of your actual touchscreen.
#define TOUCH_RAW_X_MIN        200
#define TOUCH_RAW_X_MAX        3900
#define TOUCH_RAW_Y_MIN        200
#define TOUCH_RAW_Y_MAX        3900

// Set to 1 if the raw touch X and Y axes need exchanging.
#define TOUCH_SWAP_XY          0

// Set to 1 to reverse the respective mapped axis.
#define TOUCH_INVERT_X         0
#define TOUCH_INVERT_Y         0

// Print raw and mapped touch coordinates to Serial.
#define TOUCH_DIAGNOSTICS      1

// Button layout: two columns, six rows, in portrait orientation.
#define BUTTON_COUNT           12
#define BUTTON_COLUMNS         2
#define BUTTON_ROWS            6

#define BUTTON_MARGIN          6
#define BUTTON_GAP             4
#define BUTTON_TOP             78
#define BUTTON_BOTTOM          6

// Adjustment behaviour.
#define TAP_STEP               5
#define HOLD_STEP              5
#define LONG_PRESS_MS          700
#define HOLD_REPEAT_MS         100
#define RELEASE_DEBOUNCE_MS    30

// Display colours.
#define SCREEN_BACKGROUND      TFT_BLACK
#define TEXT_COLOUR            TFT_WHITE
#define BUTTON_BORDER_COLOUR   TFT_WHITE

// ============================================================================
// 2. HARDWARE OBJECTS
// ============================================================================

TFT_eSPI tft = TFT_eSPI();

// The CYD touch controller uses its own SPI pins/bus.
SPIClass touchSPI(HSPI);
XPT2046_Touchscreen touch(TOUCH_CS, TOUCH_IRQ);

Adafruit_NeoPixel pixels(
  NUM_LEDS,
  LED_PIN,
  LED_PIXEL_TYPE
);

// ============================================================================
// 3. SCREEN AND BUTTON GEOMETRY
// ============================================================================

int screenWidth = 240;
int screenHeight = 320;

int buttonWidth = 0;
int buttonHeight = 0;

// ============================================================================
// 4. APPLICATION COLOUR STATE
// ============================================================================

typedef struct {
  uint8_t r;
  uint8_t g;
  uint8_t b;
} RGBColour;

RGBColour currentColour = {0, 0, 0};

bool colourDisplayDirty = true;
bool ledOutputDirty = true;

// ============================================================================
// 5. BUTTON IDENTITIES
// ============================================================================

typedef enum {
  RED_DOWN = 0,
  RED_UP,

  GREEN_DOWN,
  GREEN_UP,

  BLUE_DOWN,
  BLUE_UP,

  CYAN_DOWN,
  CYAN_UP,

  MAGENTA_DOWN,
  MAGENTA_UP,

  YELLOW_DOWN,
  YELLOW_UP
} ColourAction;

const char *buttonLabels[BUTTON_COUNT] = {
  "R -", "R +",
  "G -", "G +",
  "B -", "B +",
  "C -", "C +",
  "M -", "M +",
  "Y -", "Y +"
};

// Normal and pressed colours for the six button pairs.
const uint16_t buttonColours[6][2] = {
  {0x8000, 0xF800},  // Red
  {0x0320, 0x07E0},  // Green
  {0x0010, 0x001F},  // Blue
  {0x03EF, 0x07FF},  // Cyan
  {0x8010, 0xF81F},  // Magenta
  {0x8400, 0xFFE0}   // Yellow
};

// ============================================================================
// 6. TOUCH STATE
// ============================================================================

bool touchActive = false;
bool holdProcessed = false;
bool releasePending = false;

int activeButton = -1;

uint32_t touchStartTime = 0;
uint32_t releaseStartTime = 0;
uint32_t nextHoldTime = 0;

// ============================================================================
// 7. FUNCTION DECLARATIONS
// ============================================================================

void drawInterface();
void drawButton(int id, bool pressed);
void updateColourDisplay();

void processTouch();
int findButton(int x, int y);

void convertTouch(
  int rawX,
  int rawY,
  int *screenX,
  int *screenY
);

void applyColourAction(int id, int amount);
void adjustValue(uint8_t *value, int amount);

void updateLEDs();

// ============================================================================
// 8. INDIVIDUAL TAP AND HOLD HANDLERS
// ============================================================================

void tapButton0()  { applyColourAction(0, TAP_STEP); }
void tapButton1()  { applyColourAction(1, TAP_STEP); }
void tapButton2()  { applyColourAction(2, TAP_STEP); }
void tapButton3()  { applyColourAction(3, TAP_STEP); }
void tapButton4()  { applyColourAction(4, TAP_STEP); }
void tapButton5()  { applyColourAction(5, TAP_STEP); }
void tapButton6()  { applyColourAction(6, TAP_STEP); }
void tapButton7()  { applyColourAction(7, TAP_STEP); }
void tapButton8()  { applyColourAction(8, TAP_STEP); }
void tapButton9()  { applyColourAction(9, TAP_STEP); }
void tapButton10() { applyColourAction(10, TAP_STEP); }
void tapButton11() { applyColourAction(11, TAP_STEP); }

void holdButton0()  { applyColourAction(0, HOLD_STEP); }
void holdButton1()  { applyColourAction(1, HOLD_STEP); }
void holdButton2()  { applyColourAction(2, HOLD_STEP); }
void holdButton3()  { applyColourAction(3, HOLD_STEP); }
void holdButton4()  { applyColourAction(4, HOLD_STEP); }
void holdButton5()  { applyColourAction(5, HOLD_STEP); }
void holdButton6()  { applyColourAction(6, HOLD_STEP); }
void holdButton7()  { applyColourAction(7, HOLD_STEP); }
void holdButton8()  { applyColourAction(8, HOLD_STEP); }
void holdButton9()  { applyColourAction(9, HOLD_STEP); }
void holdButton10() { applyColourAction(10, HOLD_STEP); }
void holdButton11() { applyColourAction(11, HOLD_STEP); }

// Function pointer tables keep event dispatch separate from touch handling.
typedef void (*ButtonHandler)(void);

ButtonHandler tapHandlers[BUTTON_COUNT] = {
  tapButton0, tapButton1,
  tapButton2, tapButton3,
  tapButton4, tapButton5,
  tapButton6, tapButton7,
  tapButton8, tapButton9,
  tapButton10, tapButton11
};

ButtonHandler holdHandlers[BUTTON_COUNT] = {
  holdButton0, holdButton1,
  holdButton2, holdButton3,
  holdButton4, holdButton5,
  holdButton6, holdButton7,
  holdButton8, holdButton9,
  holdButton10, holdButton11
};

// ============================================================================
// 9. COLOUR PROCESSING
// ============================================================================

// Saturating adjustment: values stay within 0..255 and never wrap.
void adjustValue(uint8_t *value, int amount)
{
  int newValue = (int)(*value) + amount;

  if (newValue < 0) {
    newValue = 0;
  } else if (newValue > 255) {
    newValue = 255;
  }

  if (*value != (uint8_t)newValue) {
    *value = (uint8_t)newValue;
    colourDisplayDirty = true;
    ledOutputDirty = true;
  }
}

// RGB is the stored colour state. CMY are complementary controls:
// C+ reduces R; M+ reduces G; Y+ reduces B.
void applyColourAction(int id, int amount)
{
  switch (id) {
    case RED_DOWN:
      adjustValue(&currentColour.r, -amount);
      break;

    case RED_UP:
      adjustValue(&currentColour.r, amount);
      break;

    case GREEN_DOWN:
      adjustValue(&currentColour.g, -amount);
      break;

    case GREEN_UP:
      adjustValue(&currentColour.g, amount);
      break;

    case BLUE_DOWN:
      adjustValue(&currentColour.b, -amount);
      break;

    case BLUE_UP:
      adjustValue(&currentColour.b, amount);
      break;

    case CYAN_DOWN:
      adjustValue(&currentColour.r, amount);
      break;

    case CYAN_UP:
      adjustValue(&currentColour.r, -amount);
      break;

    case MAGENTA_DOWN:
      adjustValue(&currentColour.g, amount);
      break;

    case MAGENTA_UP:
      adjustValue(&currentColour.g, -amount);
      break;

    case YELLOW_DOWN:
      adjustValue(&currentColour.b, amount);
      break;

    case YELLOW_UP:
      adjustValue(&currentColour.b, -amount);
      break;

    default:
      break;
  }
}

// ============================================================================
// 10. DISPLAY
// ============================================================================

void drawInterface()
{
  tft.fillScreen(SCREEN_BACKGROUND);

  updateColourDisplay();

  for (int i = 0; i < BUTTON_COUNT; i++) {
    drawButton(i, false);
  }
}

void updateColourDisplay()
{
  uint16_t previewColour = tft.color565(
    currentColour.r,
    currentColour.g,
    currentColour.b
  );

  // Preview swatch.
  tft.fillRoundRect(
    8, 8, 52, 58, 5, previewColour
  );

  tft.drawRoundRect(
    8, 8, 52, 58, 5, TFT_WHITE
  );

  char text[48];

  // RGB values.
  snprintf(
    text,
    sizeof(text),
    "R:%03u G:%03u B:%03u",
    (unsigned int)currentColour.r,
    (unsigned int)currentColour.g,
    (unsigned int)currentColour.b
  );

  tft.setTextColor(TEXT_COLOUR, SCREEN_BACKGROUND);
  tft.drawString(text, 70, 20, 1);

  // Derived CMY values.
  snprintf(
    text,
    sizeof(text),
    "C:%03u M:%03u Y:%03u",
    (unsigned int)(255 - currentColour.r),
    (unsigned int)(255 - currentColour.g),
    (unsigned int)(255 - currentColour.b)
  );

  tft.drawString(text, 70, 42, 1);

  colourDisplayDirty = false;
}

void drawButton(int id, bool pressed)
{
  if (id < 0 || id >= BUTTON_COUNT) {
    return;
  }

  int column = id % BUTTON_COLUMNS;
  int row = id / BUTTON_COLUMNS;

  int x = BUTTON_MARGIN +
          column * (buttonWidth + BUTTON_GAP);

  int y = BUTTON_TOP +
          row * (buttonHeight + BUTTON_GAP);

  int colourGroup = id / 2;

  uint16_t fillColour = pressed
    ? buttonColours[colourGroup][1]
    : buttonColours[colourGroup][0];

  tft.fillRoundRect(
    x, y,
    buttonWidth, buttonHeight,
    5, fillColour
  );

  tft.drawRoundRect(
    x, y,
    buttonWidth, buttonHeight,
    5, BUTTON_BORDER_COLOUR
  );

  tft.setTextColor(TFT_WHITE, fillColour);

  tft.drawCentreString(
    buttonLabels[id],
    x + buttonWidth / 2,
    y + buttonHeight / 2 - 7,
    2
  );
}

// ============================================================================
// 11. TOUCH COORDINATE CONVERSION
// ============================================================================

void convertTouch(
  int rawX,
  int rawY,
  int *screenX,
  int *screenY
)
{
  int rx = rawX;
  int ry = rawY;

#if TOUCH_SWAP_XY
  int temp = rx;
  rx = ry;
  ry = temp;
#endif

  int x = map(
    rx,
    TOUCH_RAW_X_MIN,
    TOUCH_RAW_X_MAX,
    0,
    screenWidth - 1
  );

  int y = map(
    ry,
    TOUCH_RAW_Y_MIN,
    TOUCH_RAW_Y_MAX,
    0,
    screenHeight - 1
  );

#if TOUCH_INVERT_X
  x = screenWidth - 1 - x;
#endif

#if TOUCH_INVERT_Y
  y = screenHeight - 1 - y;
#endif

  x = constrain(x, 0, screenWidth - 1);
  y = constrain(y, 0, screenHeight - 1);

  *screenX = x;
  *screenY = y;
}

// ============================================================================
// 12. BUTTON HIT TEST
// ============================================================================

int findButton(int x, int y)
{
  for (int id = 0; id < BUTTON_COUNT; id++) {
    int column = id % BUTTON_COLUMNS;
    int row = id / BUTTON_COLUMNS;

    int bx = BUTTON_MARGIN +
             column * (buttonWidth + BUTTON_GAP);

    int by = BUTTON_TOP +
             row * (buttonHeight + BUTTON_GAP);

    if (
      x >= bx &&
      x < bx + buttonWidth &&
      y >= by &&
      y < by + buttonHeight
    ) {
      return id;
    }
  }

  return -1;
}

// ============================================================================
// 13. TOUCH EVENT PROCESSING
// ============================================================================

void processTouch()
{
  uint32_t now = millis();
  bool pressed = touch.touched();

  // New contact.
  if (pressed && !touchActive) {
    TS_Point point = touch.getPoint();

    int x;
    int y;

    convertTouch(point.x, point.y, &x, &y);

#if TOUCH_DIAGNOSTICS
    Serial.printf(
      "Touch raw X=%d Y=%d Z=%d; screen X=%d Y=%d\n",
      point.x, point.y, point.z, x, y
    );
#endif

    int id = findButton(x, y);

    if (id >= 0) {
      touchActive = true;
      holdProcessed = false;
      releasePending = false;

      activeButton = id;
      touchStartTime = now;

      drawButton(id, true);
    }

    return;
  }

  // Contact remains down.
  if (pressed && touchActive) {
    // A renewed contact cancels a pending release debounce.
    releasePending = false;

    if (!holdProcessed) {
      if ((uint32_t)(now - touchStartTime) >= LONG_PRESS_MS) {
        holdProcessed = true;

        holdHandlers[activeButton]();

        nextHoldTime = now + HOLD_REPEAT_MS;
      }
    } else {
      // Signed subtraction makes this comparison safe across millis() wrap.
      if ((int32_t)(now - nextHoldTime) >= 0) {
        holdHandlers[activeButton]();
        nextHoldTime = now + HOLD_REPEAT_MS;
      }
    }

    return;
  }

  // Contact has gone away.
  if (!pressed && touchActive) {
    if (!releasePending) {
      releasePending = true;
      releaseStartTime = now;
    }

    // Confirm that release is stable.
    if ((uint32_t)(now - releaseStartTime) >= RELEASE_DEBOUNCE_MS) {
      // A short press produces one tap.
      // A long press has already produced its hold action(s).
      if (!holdProcessed) {
        tapHandlers[activeButton]();
      }

      drawButton(activeButton, false);

      touchActive = false;
      holdProcessed = false;
      releasePending = false;
      activeButton = -1;
    }
  }
}

// ============================================================================
// 14. WS2812 OUTPUT
// ============================================================================

void updateLEDs()
{
  if (!ledOutputDirty) {
    return;
  }

  uint32_t colour = pixels.Color(
    currentColour.r,
    currentColour.g,
    currentColour.b
  );

  // Apply the selected colour to every configured LED.
  pixels.fill(colour, 0, NUM_LEDS);
  pixels.show();

  ledOutputDirty = false;
}

// ============================================================================
// 15. SETUP
// ============================================================================

void setup()
{
  Serial.begin(115200);
  delay(100);

  Serial.println();
  Serial.println("CYD Colour Controller starting...");

  // Backlight.
  pinMode(BACKLIGHT_PIN, OUTPUT);
  digitalWrite(BACKLIGHT_PIN, HIGH);

  // Display.
  tft.init();
  tft.setRotation(DISPLAY_ROTATION);

  screenWidth = tft.width();
  screenHeight = tft.height();

  // Calculate button dimensions from actual display dimensions.
  buttonWidth = (
    screenWidth -
    (2 * BUTTON_MARGIN) -
    ((BUTTON_COLUMNS - 1) * BUTTON_GAP)
  ) / BUTTON_COLUMNS;

  buttonHeight = (
    screenHeight -
    BUTTON_TOP -
    BUTTON_BOTTOM -
    ((BUTTON_ROWS - 1) * BUTTON_GAP)
  ) / BUTTON_ROWS;

  // Touchscreen SPI bus.
  touchSPI.begin(
    TOUCH_SCK,
    TOUCH_MISO,
    TOUCH_MOSI,
    TOUCH_CS
  );

  if (!touch.begin(touchSPI)) {
    Serial.println("Touchscreen initialisation failed.");
  }

  // Do not use touch.setRotation() here: coordinate orientation is
  // handled explicitly by the calibration and conversion function above.

  // WS2812.
  pixels.begin();
  pixels.setBrightness(LED_BRIGHTNESS);
  pixels.clear();
  pixels.show();

  // Draw initial interface and send initial black LED colour.
  drawInterface();

  ledOutputDirty = true;
  updateLEDs();

  Serial.println("--------------------------------");
  Serial.printf("Display: %d x %d\n", screenWidth, screenHeight);
  Serial.printf("Button dimensions: %d x %d\n", buttonWidth, buttonHeight);
  Serial.printf("WS2812 GPIO: %d\n", LED_PIN);
  Serial.printf("WS2812 count: %d\n", NUM_LEDS);
  Serial.println("Initialisation complete.");
  Serial.println("--------------------------------");
}

// ============================================================================
// 16. MAIN LOOP
// ============================================================================

void loop()
{
  processTouch();

  if (colourDisplayDirty) {
    updateColourDisplay();
  }

  if (ledOutputDirty) {
    updateLEDs();
  }

  // Application-specific non-blocking processing can be added here.
}
