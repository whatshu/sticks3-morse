"""
PlatformIO post-upload script for M5Stack StickS3.

After flashing, the StickS3 (ESP32-S3-PICO-1) stays in ROM download mode
because the USB-Serial-JTAG DTR assertion triggers download mode on every
port open.  The standard "Hard resetting via RTS pin" does not boot the
firmware.

This script uses esptool with --no-stub --after soft_reset to tell the
ROM bootloader to perform a software reset into the user firmware.
"""
import subprocess
import sys
from os.path import expanduser, join

Import("env")

def after_upload(source, target, env):
    port = env.GetProjectOption("upload_port", "/dev/ttyACM0")
    # Find esptool.py in the PlatformIO packages
    esptool = join(
        expanduser("~"),
        ".platformio/packages/tool-esptoolpy/esptool.py"
    )
    print(f"\n=== Booting firmware via soft_reset (StickS3 workaround) ===")
    result = subprocess.run(
        [
            sys.executable, esptool,
            "--port", port,
            "--before", "no_reset",
            "--after", "soft_reset",
            "--no-stub",
            "read_mac"
        ],
        capture_output=False,
        timeout=30
    )
    if result.returncode == 0:
        print("Firmware booted successfully. Check the LCD display.")
        print("To read serial output, wait 3s then connect with:")
        print(f"  picocom --baud 115200 {port}")
        print("  (opening the serial port may re-trigger download mode;")
        print("   if so, physically unplug/replug USB-C after flashing)")
    else:
        print("WARNING: soft_reset failed. Try unplugging/replugging USB-C.")

env.AddPostAction("upload", after_upload)
