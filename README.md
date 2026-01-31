🚀 ESP32 GIF Matrix Controller

This project transforms an ESP32 and a HUB75 LED Matrix into a Wi-Fi-enabled digital signage platform. It allows for wireless GIF uploads, file management, and real-time brightness control via a web dashboard.
🛠 Hardware Configuration

    Controller: ESP32 (38-pin DevKit recommended).

    Display: 64x64 HUB75 LED Panels tested up to 3x (192x64 total resolution).

    Power: 5V High-Current Supply (minimum 10A recommended for 3 panels).

    
```
+-------------------------+-----------------+--------------------+
| ESP32 Pin               | HUB75 Pin       | Function           |
+-------------------------+-----------------+--------------------+
| GPIO 26, 27, 25         | R1, G1, B1      | Upper RGB Data     |
| GPIO 12, 13, 14         | R2, G2, B2      | Lower RGB Data     |
| GPIO 23, 19, 5, 17, 32  | A, B, C, D, E   | Row Address Lines  |
| GPIO 4, 15, 16          | LAT, OE, CLK    | Control Lines      |
+-------------------------+-----------------+--------------------+
```
🌐 Web Interface Features

    Dashboard: View "Now Playing" status and adjust brightness via synced slider/number inputs.

    File Manager: Upload new GIFs, play specific files, or delete unwanted content.

    Diagnostics: Monitor real-time system health, including RAM Pressure, Wi-Fi signal strength (RSSI), and system uptime.

    Auto-Play: Automatically cycles through all stored GIFs on a 15-second timer (configurable).

💾 Installation

    Libraries: Install the following via the Arduino Library Manager:

        ESP32 HUB75 LED MATRIX INTERFACE ICSS DMA

        AnimatedGIF

    Filesystem: Ensure your ESP32 is configured for LittleFS.

    Wi-Fi: Update the sta_ssid and sta_password variables in the code to match your home network.

    Upload:

        Set Partition Scheme to Default 4MB with spiffs or any scheme providing enough room for LittleFS.

        Close the Serial Monitor before uploading to avoid COM Port Access Denied errors.

📋 Usage Instructions

    Connect: Join the Wi-Fi network Matrix-Sign-AP (Password: Password1) or access it via your local network IP shown on the Serial Monitor.

    Navigate: Open http://192.168.42.1 in any web browser.

    Upload: Choose a GIF. Note: For each panel can do up to 64x64, for 3 panels GIFs should ideally be 192x64 pixels.
    You can use a tool like ezgif to create or resize files. https://ezgif.com/resize

    Diagnostics: If the web page feels laggy, check the Diagnostics page. High "RAM Pressure" (above 80%) may indicate the GIF file size is too large for the ESP32 to handle comfortably.

⚠️ Troubleshooting

    Hanging or breaking on Upload: This is often a power issue. Reduce brightness to 10% and try again or make the first gif blank for lowest power draw. 

    Ghosting/Leftover Pixels: The code is designed to clearScreen() only when switching files. If a GIF has a transparent background, it may leave trails; ensure your GIFs have a solid background for best results.
