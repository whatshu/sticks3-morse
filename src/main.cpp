/**
 * Hello World for M5Stack StickS3
 *
 * Initializes the M5Unified library and displays "Hello World" on the
 * built-in LCD screen while also outputting to the serial monitor.
 *
 * StickS3 setup notes (learned the hard way):
 * - USB CDC must be enabled (ARDUINO_USB_CDC_ON_BOOT=1) for serial output.
 * - After flashing, the board may not appear on /dev/ttyACM0 immediately;
 *   it can take 1–3 seconds for the USB CDC device to enumerate.
 * - The board has PSRAM. The build flag -mfix-esp32-psram-cache-issue
 *   resolves a known silicon bug with PSRAM cache access on ESP32-S3.
 * - M5Unified auto-detects the board variant; no manual pin configuration
 *   is needed for basic LCD, button, and buzzer use.
 * - PlatformIO may need udev rules to access /dev/ttyACM0 without sudo.
 *   If upload fails with "Permission denied", add your user to the
 *   `dialout` group or install udev rules for the ESP32-S3 USB device.
 */

#include <M5Unified.h>

void setup() {
  // Initialize M5Unified with full feature set.
  // - M5.begin() auto-detects the board, initializes LCD, power management,
  //   and peripheral I2C (IMU, RTC).
  // - The StickS3 LCD is 128x128 pixels, rotation 0 = USB port at bottom.
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;
  M5.begin(cfg);

  // Serial output — requires USB CDC enabled in build flags.
  Serial.println("=== M5Stack StickS3 Hello World ===");
  Serial.println("Board: StickS3 (ESP32-S3)");
  Serial.println("LCD:  128x128 TFT (ST7789 via M5Unified)");
  Serial.println("==================================\n");

  // LCD display
  M5.Lcd.fillScreen(TFT_BLACK);
  M5.Lcd.setTextColor(TFT_WHITE);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(10, 40);
  M5.Lcd.println("Hello");
  M5.Lcd.setCursor(10, 70);
  M5.Lcd.println("World!");

  // Small footer
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(TFT_GREEN);
  M5.Lcd.setCursor(10, 110);
  M5.Lcd.println("StickS3 Ready.");

  Serial.println("Display updated. Setup complete.");
}

void loop() {
  // Blink the power LED (green) every second to show the board is alive.
  // M5.Power manages the LED for supported boards.
  static unsigned long last_toggle = 0;
  static bool led_state = false;

  unsigned long now = millis();
  if (now - last_toggle >= 1000) {
    last_toggle = now;
    led_state = !led_state;

    // Toggle the built-in LED via M5Unified power API.
    // On StickS3, this controls the green power LED.
    if (led_state) {
      M5.Power.setLed(1);
    } else {
      M5.Power.setLed(0);
    }

    // Periodic serial heartbeat
    static unsigned long beat_count = 0;
    Serial.printf("Heartbeat #%lu — %s\n",
                  ++beat_count,
                  led_state ? "LED ON" : "LED OFF");
  }

  // Small delay to yield CPU; M5.update() handles internal state.
  M5.update();
  delay(10);
}
