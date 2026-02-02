🚀 ESP32 GIF Matrix Controller

This project transforms an ESP32 and a HUB75 LED Matrix into a Wi-Fi-enabled digital signage platform. It features a modern web dashboard for wireless GIF uploads, file management, and real-time matrix control.
🛠 Hardware Configuration

    Controller: ESP32 (38-pin DevKit recommended).

    Display: 64x64 HUB75 LED Panels.

     Power: 5V High-Current Supply (minimum 10A recommended for 3 panels).


   
This is the Pinout I'm using for my panel, it may be different for your board.
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

🌐 Web Interface & Features
Dashboard & Real-time Controls

    Synced Inputs: Adjust Brightness (0-255) and Loop Counts (1-20) using either a slider or by typing a specific number. The two inputs stay perfectly in sync.

    Now Playing: View the current active filename and playback mode.

    Auto-Playlist: Automatically cycles through all stored GIFs. Unlike a simple timer, the controller now tracks the GIF's internal animation and ensures it plays a full cycle (or multiple) before switching.

File & WiFi Management

    Gallery: View all uploaded files with file sizes (KB) and instant "Play" or "Delete" actions.

    Smart Uploads: Direct upload via the dashboard with an automatic redirect to your file gallery upon completion.

    WiFi Manager: Add credentials to connect to your home network under the settings page.

Diagnostics

    System Health: Monitor real-time RAM Pressure, Wi-Fi Signal Strength (RSSI), and Storage Usage with visual progress bars showing used vs. maximum capacity.

    Remote Reboot: Restart the controller wirelessly via the Settings page.

💾 Installation

    Libraries: Install via Arduino Library Manager:

        ESP32 HUB75 LED MATRIX INTERFACE I2S DMA

        AnimatedGIF

    Filesystem: The project uses LittleFS. Ensure your board is configured for it.

    Partition Scheme: Set to Default 4MB with SPIFFS (or any scheme providing ~1.5MB+ for LittleFS).

    Upload: Close the Serial Monitor before uploading to avoid COM port conflicts.

📋 Usage Instructions

    Initial Connection: On first boot, join the Wi-Fi network Matrix-Sign-AP (Password: Password1).

    Navigate: Open http://192.168.4.1 in your browser.

    Configure WiFi: Go to Settings, enter your home WiFi credentials, and click Save & Connect. The device will reboot and join your network.

    Optimizing GIFs: For a single 64x64 panel, use 64x64 GIFs. For 3 panels, use 192x64. Use for easy resizing.

⚠️ Troubleshooting

    Upload Failures: Often caused by power sag. Lower brightness to 3 (default) during large uploads.

    Laggy Interface: Check the Diagnostics page. If RAM Pressure is consistently above 80%, the GIF frame size or color depth might be too complex for the ESP32.

    Ghosting: Ensure GIFs have a solid background. Transparent GIFs can leave "trails" as the screen only clears during file transitions to prevent flickering.
