/**
 * Hello World for M5Stack StickS3
 *
 * Initializes M5Unified and displays "Hello World" on the built-in LCD.
 * Also outputs heartbeat messages via USB CDC serial.
 *
 * == Hardware ==
 * The StickS3 uses an ESP32-S3-PICO-1 with:
 *   - 8MB Flash (GD), 8MB PSRAM (AP_3v3)
 *   - 128x128 TFT LCD (ST7789, driven by M5GFX via M5Unified)
 *   - 6-axis IMU (BMI270), RTC (BM8563), Buzzer, IR TX, Button
 *   - Built-in USB-Serial/JTAG (no external USB-UART chip)
 *
 * == Build Configuration (platformio.ini) ==
 *   platform = espressif32@6.12.0
 *   board = esp32-s3-devkitc-1  (no native StickS3 definition exists)
 *   framework = arduino
 *   board_build.arduino.partitions = default_8MB.csv
 *   board_build.arduino.memory_type = qio_opi
 *
 *   build_flags:
 *     -DESP32S3                     → identify chip
 *     -DBOARD_HAS_PSRAM             → enable PSRAM in Arduino layer
 *     -mfix-esp32-psram-cache-issue → workaround ESP32-S3 PSRAM silicon bug
 *     -DCORE_DEBUG_LEVEL=5          → verbose logging
 *     -DARDUINO_USB_CDC_ON_BOOT=1   → Serial maps to USB CDC (not UART0)
 *     -DARDUINO_USB_MODE=1          → hardware CDC (USB-Serial/JTAG)
 *
 * == Known Issues & Workarounds ==
 *
 * 1. USB-Serial/JTAG DTR/reset behavior:
 *    On the StickS3 (ESP32-S3-PICO-1), asserting DTR (setting it LOW) puts
 *    the chip into DOWNLOAD mode regardless of RTS state.  The Linux CDC ACM
 *    driver asserts DTR EVERY time /dev/ttyACM0 is opened.  This creates a
 *    chicken-and-egg problem:
 *      - Opening the serial port to read output → DTR asserted → chip resets
 *        → ROM bootloader enters download mode → firmware stops running.
 *
 *    WORKAROUND: After PlatformIO flashes the firmware and does "Hard resetting
 *    via RTS pin", the chip may be stuck in download mode.  To run the firmware:
 *      a) Physically unplug and replug the USB-C cable (power cycle).
 *         The chip boots without DTR assertion and runs the firmware normally.
 *      b) After the firmware is running (LCD shows "Hello World"), you can
 *         safely open /dev/ttyACM0 — the firmware's Serial.begin() prevents
 *         the ROM-level DTR reset from re-triggering.
 *
 *    If you use `pio device monitor`, it opens the port and triggers the
 *    reset.  Use an external terminal (screen, minicom, picocom) AFTER
 *    power-cycling the board.
 *
 * 2. HWCDC::begin() does NOT deinit the ROM's USB configuration:
 *    In Arduino-ESP32 3.x (framework-arduinoespressif32 @ 3.20017.241212),
 *    HWCDC::begin() has the `deinit()` call commented out.  This means the
 *    USB device never re-enumerates after the ROM bootloader hands over to
 *    the firmware.  The host may keep stale CDC endpoint configuration.
 *
 *    WORKAROUND: Directly manipulate the USB_SERIAL_JTAG registers to
 *    force a USB disconnect/reconnect before initializing serial.
 *    - Set dp_pullup=0 + usb_pad_enable=0 → host sees disconnect
 *    - Delay 200ms
 *    - Set dp_pullup=1 + usb_pad_enable=1 → host sees new device → re-enumerates
 *    HWCDC::deinit() doesn't work for this because pinMode() can't
 *    override the USB PHY when usb_pad_enable is set.
 *
 * 3. PSRAM board definition mismatch:
 *    The esp32-s3-devkitc-1 board is defined as "No PSRAM" in PlatformIO,
 *    but the StickS3 has 8MB PSRAM.  The -DBOARD_HAS_PSRAM flag tells the
 *    Arduino layer to use PSRAM, but the underlying ESP-IDF sdkconfig may
 *    still have PSRAM disabled.  This appears to work in practice (the PSRAM
 *    is detected by esptool and the firmware boots), but be aware of the
 *    potential mismatch.
 */

#include <M5Unified.h>
// For direct USB_SERIAL_JTAG register access (force USB re-enumeration)
#include "hal/usb_serial_jtag_ll.h"

void setup() {
  // == Step 1: Force USB re-enumeration at the register level ==
  // The ROM bootloader leaves the USB-Serial/JTAG peripheral configured.
  // HWCDC::begin() does NOT trigger a USB reset (its deinit() is commented
  // out).  We force a disconnect by clearing the D+ pullup and disabling the
  // USB pad via direct register writes.  The host sees a disconnect; when we
  // re-enable, it sees a new connection and properly enumerates the CDC ACM
  // interface under firmware control.
  USB_SERIAL_JTAG.conf0.dp_pullup = 0;      // D+ pullup off → disconnect
  USB_SERIAL_JTAG.conf0.usb_pad_enable = 0; // USB pad off
  delay(200);
  USB_SERIAL_JTAG.conf0.dp_pullup = 1;      // D+ pullup on → host sees connect
  USB_SERIAL_JTAG.conf0.usb_pad_enable = 1; // USB pad on
  delay(200);                                // Host enumerates

  // == Step 2: Initialize serial ==
  Serial.begin(115200);
  // Use a timed delay rather than `while(!Serial)` because on hardware CDC
  // the connection flag may already be true before the host opens the port.
  delay(2000);

  Serial.println("\n\n=== M5Stack StickS3 Hello World ===");
  Serial.println("Board:  StickS3 (ESP32-S3-PICO-1)");
  Serial.println("Chip:   ESP32-S3 rev v0.2");
  Serial.println("Flash:  8MB (GD QIO)");
  Serial.println("PSRAM:  8MB (AP_3v3 OPI)");
  Serial.println("LCD:    128x128 TFT (ST7789)");
  Serial.println("USB:    HW CDC (USB-Serial/JTAG)");
  Serial.println("==================================\n");
  Serial.println("Initializing M5Unified...");

  // == Step 3: Initialize M5Unified ==
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;
  M5.begin(cfg);

  Serial.println("M5Unified initialized successfully.");

  // == Step 4: LCD display ==
  M5.Lcd.fillScreen(TFT_BLACK);
  M5.Lcd.setTextColor(TFT_WHITE);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(10, 40);
  M5.Lcd.println("Hello");
  M5.Lcd.setCursor(10, 70);
  M5.Lcd.println("World!");

  // Footer
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(TFT_GREEN);
  M5.Lcd.setCursor(10, 110);
  M5.Lcd.println("StickS3 Ready.");

  Serial.println("Display updated. Setup complete.\n");
}

void loop() {
  static unsigned long last_toggle = 0;
  static bool led_state = false;

  unsigned long now = millis();
  if (now - last_toggle >= 1000) {
    last_toggle = now;
    led_state = !led_state;

    // Toggle power LED (green)
    M5.Power.setLed(led_state ? 1 : 0);

    static unsigned long beat_count = 0;
    Serial.printf("Heartbeat #%lu — LED %s\n",
                  ++beat_count,
                  led_state ? "ON" : "OFF");
  }

  M5.update();
  delay(10);
}
