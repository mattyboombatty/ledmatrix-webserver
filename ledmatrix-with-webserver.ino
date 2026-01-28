// This sketch creates a Wi-Fi-enabled web server for uploading images and displaying them
// on an LED matrix.
//
// Features:
// - Serves a web page to upload new GIF files.
// - Adds a file manager page to list, play, and delete uploaded files.
// - Automatically plays all GIFs in the file system on boot.
// - Adds a "Play All" button to manually start the continuous playback.
// - Includes a brightness control slider on the upload page.
// - NEW: Adds a slider to control the "Play All" duration.
// - NEW: Configured for three 64x64 panels chained horizontally with the controller on the right panel.
//
// Credits:
// Based on the work of bitbank2 (AnimatedGIF library) and Random Nerd Tutorials (web server).
//
#include "FS.h"
#include <LittleFS.h>
#include <AnimatedGIF.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <WiFi.h>
#include <WebServer.h>

#define FILESYSTEM LittleFS
#define FORMAT_LITTLEFS_IF_FAILED true

#define PANEL_RES_X 64     // Number of pixels wide of each INDIVIDUAL panel module.
#define PANEL_RES_Y 64     // Number of pixels tall of each INDIVIDUAL panel module.
#define PANEL_CHAIN 3      // Total number of panels chained one to another horizontally only.

// Define the SSID and password for the ESP32's Access Point (AP)
const char* ap_ssid = "Matrix-Sign-AP";
const char* ap_password = "Password1";

// Define the SSID and password for the network you want the ESP32 to connect to (Station/STA)
// CHANGE THESE TO YOUR HOME WI-FI CREDENTIALS
const char* sta_ssid = "your_ssid";
const char* sta_password = "your_password";

WebServer server(80);

MatrixPanel_I2S_DMA *dma_display = nullptr;

uint16_t myBLACK, myWHITE, myRED, myGREEN, myBLUE;

AnimatedGIF gif;
File f;
int x_offset, y_offset;

// Variable to store the path of the newly uploaded file
String uploadedFilePath = "";
bool newFileUploaded = false;
String currentPlayingFile = "";
int currentBrightness = 180; // Default brightness

// State variable to control if we are in "play all" mode or "single play" mode
bool playAllMode = false;
// A file object to keep track of the directory listing for "play all" mode
File root;
// Timer for "play all" mode, each GIF will play for a specified duration
unsigned long lastPlaybackChange = 0;
unsigned long playbackDuration = 15000; // Default duration in milliseconds (15 seconds)

// Draw a line of image directly on the LED Matrix
void GIFDraw(GIFDRAW *pDraw)
{
  uint8_t *s;
  uint16_t *d, *usPalette, usTemp[320];
  int x, y, iWidth;

  iWidth = pDraw->iWidth;
  if (iWidth > dma_display->width())
    iWidth = dma_display->width();

  usPalette = pDraw->pPalette;
  y = pDraw->iY + pDraw->y + y_offset; // current line with offset

  s = pDraw->pPixels;
  if (pDraw->ucDisposalMethod == 2) // restore to background color
  {
    for (x=0; x<iWidth; x++)
    {
      if (s[x] == pDraw->ucTransparent)
        s[x] = pDraw->ucBackground;
    }
    pDraw->ucHasTransparency = 0;
  }
  // Apply the new pixels to the main image
  if (pDraw->ucHasTransparency) // if transparency used
  {
    uint8_t ucTransparent = pDraw->ucTransparent;
    for (x = 0; x < pDraw->iWidth; x++)
    {
      if (s[x] != ucTransparent)
      {
        // Removed the horizontal flip to address image distortion issues
        dma_display->drawPixel(pDraw->iX + x + x_offset, y, usPalette[s[x]]);
      }
    }
  }
  else // does not have transparency
  {
    s = pDraw->pPixels;
    for (x=0; x<pDraw->iWidth; x++)
    {
      // Removed the horizontal flip to address image distortion issues
      dma_display->drawPixel(pDraw->iX + x + x_offset, y, usPalette[*s++]);
    }
  }
} /* GIFDraw() */


void * GIFOpenFile(const char *fname, int32_t *pSize)
{
  Serial.print("Playing gif: ");
  Serial.println(fname);
  f = FILESYSTEM.open(fname);
  if (f)
  {
    *pSize = f.size();
    return (void *)&f;
  }
  return NULL;
} /* GIFOpenFile() */

void GIFCloseFile(void *pHandle)
{
  File *f = static_cast<File *>(pHandle);
  if (f != NULL)
      f->close();
} /* GIFCloseFile() */

int32_t GIFReadFile(GIFFILE *pFile, uint8_t *pBuf, int32_t iLen)
{
    int32_t iBytesRead;
    iBytesRead = iLen;
    File *f = static_cast<File *>(pFile->fHandle);
    if ((pFile->iSize - pFile->iPos) < iLen)
       iBytesRead = pFile->iSize - pFile->iPos - 1; // <-- ugly work-around
    if (iBytesRead <= 0)
       return 0;
    iBytesRead = (int32_t)f->read(pBuf, iBytesRead);
    pFile->iPos = f->position();
    return iBytesRead;
} /* GIFReadFile() */

int32_t GIFSeekFile(GIFFILE *pFile, int32_t iPosition)
{
  File *f = static_cast<File *>(pFile->fHandle);
  f->seek(iPosition);
  pFile->iPos = (int32_t)f->position();
  return pFile->iPos;
} /* GIFSeekFile() */

void ShowGIF(const char *name)
{
  // Close the current GIF if one is open
  if (currentPlayingFile != "") {
      gif.close();
  }
  Serial.printf("Attempting to play GIF: %s\n", name);
  if (gif.open(name, GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDraw))
  {
    x_offset = (dma_display->width() - gif.getCanvasWidth())/2;
    if (x_offset < 0) x_offset = 0;
    y_offset = (dma_display->height() - gif.getCanvasHeight())/2;
    if (y_offset < 0) y_offset = 0;
    Serial.printf("Successfully opened GIF; Canvas size = %d x %d\n", gif.getCanvasWidth(), gif.getCanvasHeight());
    Serial.flush();
    currentPlayingFile = name;
  } else {
    Serial.println("Failed to open GIF file.");
    currentPlayingFile = "";
  }
} /* ShowGIF() */

// This function plays the next file in the list
void playNextFile() {
  File file = root.openNextFile();
  if (!file) {
    // If we've reached the end, rewind and get the first file again
    Serial.println("End of file list, restarting from the beginning.");
    root.rewindDirectory();
    file = root.openNextFile();
  }
  if (file) {
    // Prepend the root path "/" to the filename to get the full path
    String fullPath = "/" + String(file.name());
    Serial.print("Attempting to play file with full path: ");
    Serial.println(fullPath);
    ShowGIF(fullPath.c_str());
    lastPlaybackChange = millis();
    file.close();
  } else {
    Serial.println("No files found on LittleFS.");
    // Clear the screen if no files are found
    dma_display->clearScreen();
  }
}

// Dedicated function to start "play all" mode
void startPlayAllMode() {
  playAllMode = true;
  root.rewindDirectory();
  playNextFile();
}

// This function serves the HTML upload page when a client connects
void handleRoot() {
  // Get storage information
  long totalBytes = LittleFS.totalBytes();
  long usedBytes = LittleFS.usedBytes();
  int percentUsed = (int)((float)usedBytes / totalBytes * 100);

  // Get the IP address for the connected STA network
  String staIp = WiFi.localIP().toString();

  // Build the HTML string dynamically with the storage info and brightness slider
  String html = "<!DOCTYPE html>"
    "<html lang='en'>"
    "<head>"
    "<meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
    "<title>ESP32 LED Matrix</title>"
    "<style>"
    "body { font-family: Arial, sans-serif; text-align: center; background-color: #222; color: #fff; }"
    ".container { margin-top: 50px; }"
    "h1 { color: #00ff00; }"
    "form { margin-top: 20px; }"
    "input[type='file'] { background-color: #333; color: #fff; padding: 10px; border-radius: 5px; border: 1px solid #555; }"
    "input[type='submit'] { background-color: #00ff00; color: #000; padding: 10px 20px; border: none; border-radius: 5px; cursor: pointer; }"
    ".storage-info { color: #aaa; margin-top: 20px; font-size: 0.9em; }"
    ".nav-links { margin-bottom: 20px; }"
    ".nav-links a { color: #00ff00; text-decoration: none; padding: 10px; border: 1px solid #00ff00; border-radius: 5px; }"
    ".controls { margin: 20px 0; display: flex; flex-direction: column; align-items: center; }"
    ".control-group { margin-bottom: 10px; }"
    "</style>"
    "</head>"
    "<body>"
    "<div class='container'>"
    "<h1>Upload New Content</h1>"
    "<div class='nav-links'>"
    "<a href='/list'>Manage Files</a>"
    "</div>"
    "<p>To connect to the device, use the Access Point: 'Matrix-Sign-AP' at <a href='http://192.168.42.1'>http://192.168.42.1</a></p>"
    "<p>Or, if you are connected to your Wi-Fi network, use this IP address: <a href='http://" + staIp + "'>" + staIp + "</a></p>"
    "<p>Select a GIF or image file to display on the matrix.</p>"
    "<form method='POST' action='/upload' enctype='multipart/form-data'>"
    "<input type='file' name='file'><br><br>"
    "<input type='submit' value='Upload'>"
    "</form>"
    "<div class='controls'>"
    "<div class='control-group'>"
    "<label for='brightness'>Brightness:</label>"
    "<input type='range' id='brightness' name='brightness' min='0' max='255' value='" + String(currentBrightness) + "'>"
    "</div>"
    "<div class='control-group'>"
    "<label for='duration'>Playlist Duration (seconds):</label>"
    "<input type='number' id='duration' name='duration' min='1' max='60' value='" + String(playbackDuration / 1000) + "'>"
    "</div>"
    "</div>"
    "<div class='storage-info'>"
    "<span>Used: " + String(usedBytes / 1024.0, 2) + " KB</span><br>"
    "<span>Total: " + String(totalBytes / 1024.0, 2) + " KB</span><br>"
    "<span>(" + String(percentUsed) + "%)</span>"
    "</div>"
    "</div>"
    "<script>"
    "var brightnessSlider = document.getElementById('brightness');"
    "brightnessSlider.addEventListener('input', function() {"
    "  var xhr = new XMLHttpRequest();"
    "  xhr.open('GET', '/brightness?value=' + this.value, true);"
    "  xhr.send();"
    "});"
    "var durationInput = document.getElementById('duration');"
    "durationInput.addEventListener('change', function() {"
    "  var xhr = new XMLHttpRequest();"
    "  xhr.open('GET', '/duration?value=' + this.value, true);"
    "  xhr.send();"
    "});"
    "</script>"
    "</body>"
    "</html>";

  server.send(200, "text/html", html);
}

// This function serves the file list page
void handleFileList() {
  String html = "<!DOCTYPE html>"
    "<html lang='en'>"
    "<head>"
    "<meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
    "<title>File Management</title>"
    "<style>"
    "body { font-family: Arial, sans-serif; text-align: center; background-color: #222; color: #fff; }"
    ".container { margin-top: 50px; }"
    "h1 { color: #00ff00; }"
    ".nav-links { margin-bottom: 20px; }"
    ".nav-links a { color: #00ff00; text-decoration: none; padding: 10px; border: 1px solid #00ff00; border-radius: 5px; }"
    ".file-list { list-style-type: none; padding: 0; }"
    ".file-list li { margin: 10px 0; padding: 10px; background-color: #333; border-radius: 5px; display: flex; justify-content: space-between; align-items: center; }"
    ".file-list a { color: #ff0000; text-decoration: none; font-weight: bold; padding: 5px 10px; border: 1px solid #ff0000; border-radius: 5px; }"
    ".file-list .play-link { color: #00ff00; border-color: #00ff00; margin-right: 10px; }"
    ".file-list .play-all-link { background-color: #00ff00; color: #000; border-color: #00ff00; }"
    "</style>"
    "</head>"
    "<body>"
    "<div class='container'>"
    "<h1>Stored Files</h1>"
    "<div class='nav-links'>"
    "<a href='/'>Back to Upload Page</a>"
    "<a href='/playall' class='play-all-link'>Play All</a>"
    "</div>"
    "<ul class='file-list'>";

  File root = LittleFS.open("/");
  File file = root.openNextFile();
  while(file){
    if (!file.isDirectory()){
      html += "<li><span>" + String(file.name()) + " (" + String(file.size()) + " bytes)</span> <div><a href='/play?file=" + String(file.name()) + "' class='play-link'>Play</a><a href='/delete?file=" + String(file.name()) + "'>Delete</a></div></li>";
    }
    file = root.openNextFile();
  }
  html += "</ul></div></body></html>";
  server.send(200, "text/html", html);
}

// This function handles playing a specific file
void handlePlay() {
  if (server.hasArg("file")) {
    uploadedFilePath = "/" + server.arg("file");
    newFileUploaded = true;
    playAllMode = false; // Disable "play all" mode
    server.sendHeader("Location", "/list");
    server.send(302); // Redirect to the file list page
  } else {
    server.send(400, "text/plain", "Bad request: No file specified.");
  }
}

// This function handles the "Play All" request
void handlePlayAll() {
  startPlayAllMode();
  server.sendHeader("Location", "/list");
  server.send(302); // Redirect to the file list page
}

// This function handles the brightness control
void handleBrightness() {
  if (server.hasArg("value")) {
    currentBrightness = server.arg("value").toInt();
    dma_display->setBrightness8(currentBrightness);
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Bad request: No value specified.");
  }
}

// NEW: This function handles the duration control
void handleDuration() {
  if (server.hasArg("value")) {
    // Convert the value from seconds to milliseconds
    playbackDuration = server.arg("value").toInt() * 1000;
    Serial.printf("New playback duration set to %lu ms\n", playbackDuration);
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Bad request: No value specified.");
  }
}

// This function handles the file deletion
void handleDelete() {
  if (server.hasArg("file")) {
    String fileName = server.arg("file");
    if (LittleFS.remove("/" + fileName)) {
      Serial.println("File deleted: " + fileName);
      server.send(200, "text/plain", "File '" + fileName + "' deleted successfully.");
    } else {
      Serial.println("Failed to delete file: " + fileName);
      server.send(500, "text/plain", "Failed to delete file '" + fileName + "'.");
    }
  } else {
    server.send(400, "text/plain", "Bad request: No file specified.");
  }
}

// This function handles the file upload
void handleUpload() {
  HTTPUpload& upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    uploadedFilePath = "/" + upload.filename;
    Serial.print("Starting upload of ");
    Serial.println(uploadedFilePath);
    
    // Check if there is enough space before starting the upload
    if (LittleFS.usedBytes() + upload.totalSize > LittleFS.totalBytes()) {
      Serial.println("Error: Not enough space to upload this file.");
      server.send(507, "text/plain", "Error: Not enough space on device.");
      return;
    }
    
    f = LittleFS.open(uploadedFilePath, FILE_WRITE);
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (f) {
      f.write(upload.buf, upload.currentSize);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (f) {
      f.close();
      Serial.println("Upload finished.");
      newFileUploaded = true;
      playAllMode = false; // Uploading a new file also disables "play all" mode
    }
  }
}

void setup() {
  Serial.begin(115200);

  // A brief delay to allow everything to initialize
  delay(100);

  // Initialize LittleFS
  if(!LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED)){
      Serial.println("LittleFS Mount Failed");
      return;
  }
  
  // Set up the ESP32 as a Wi-Fi Access Point (AP) and a Station (STA)
  WiFi.mode(WIFI_AP_STA);

  // Configure and start the AP
  Serial.print("Setting up AP. SSID: ");
  Serial.println(ap_ssid);
  IPAddress local_IP(192, 168, 42, 1);
  IPAddress gateway(192, 168, 42, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(local_IP, gateway, subnet);
  WiFi.softAP(ap_ssid, ap_password);
  Serial.print("AP IP Address: ");
  Serial.println(WiFi.softAPIP());

  // Connect to the specified Wi-Fi network
  Serial.print("Connecting to STA. SSID: ");
  Serial.println(sta_ssid);
  WiFi.begin(sta_ssid, sta_password);
  int connect_timeout = 0;
  while (WiFi.status() != WL_CONNECTED && connect_timeout < 20) {
    delay(500);
    Serial.print(".");
    connect_timeout++;
  }
  Serial.println("");

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("STA connected! IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Failed to connect to Wi-Fi network.");
  }

  // Set up the web server
  server.on("/", handleRoot);
  server.on("/list", handleFileList);
  server.on("/delete", handleDelete);
  server.on("/play", handlePlay);
  server.on("/playall", handlePlayAll);
  server.on("/brightness", handleBrightness);
  server.on("/duration", handleDuration); // NEW endpoint for duration
  server.on("/upload", HTTP_POST, [](){
    server.send(200);
  }, handleUpload);
  server.begin();
  Serial.println("HTTP server started.");

  // Initialize LED matrix
  HUB75_I2S_CFG mxconfig(
    PANEL_RES_X,
    PANEL_RES_Y,
    PANEL_CHAIN
  );

  mxconfig.gpio.r1 = 26;
  mxconfig.gpio.g1 = 27;
  mxconfig.gpio.b1 = 25;
  mxconfig.gpio.r2 = 12;
  mxconfig.gpio.g2 = 13;
  mxconfig.gpio.b2 = 14;
  mxconfig.gpio.a = 23;
  mxconfig.gpio.b = 19;
  mxconfig.gpio.c = 5;
  mxconfig.gpio.d = 17;
  mxconfig.gpio.e = 32;
  mxconfig.gpio.lat = 4;
  mxconfig.gpio.oe = 15;
  mxconfig.gpio.clk = 16;

  mxconfig.driver = HUB75_I2S_CFG::FM6126A;
  mxconfig.clkphase = false;

  dma_display = new MatrixPanel_I2S_DMA(mxconfig);
  dma_display->begin();
  dma_display->setBrightness8(currentBrightness);
  dma_display->clearScreen();

  myBLACK = dma_display->color565(0, 0, 0);
  myWHITE = dma_display->color565(255, 255, 255);
  myRED   = dma_display->color565(255, 0, 0);
  myGREEN = dma_display->color565(0, 255, 0);
  myBLUE  = dma_display->color565(0, 0, 255);

  dma_display->fillScreen(myBLACK);

  gif.begin(LITTLE_ENDIAN_PIXELS);

  // Start "play all" mode on boot
  root = LittleFS.open("/");
  startPlayAllMode();
}

void loop()
{
  // Handle new incoming HTTP requests
  server.handleClient();
  
  // Print a heartbeat message to the Serial Monitor to show the IP address
  // and confirm the loop is running.
  static unsigned long lastPrintTime = 0;
  if (millis() - lastPrintTime > 5000) {
    Serial.print("Current IP Address: ");
    Serial.println(WiFi.softAPIP());
    // Also print the STA IP if connected
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("STA IP Address: ");
      Serial.println(WiFi.localIP());
    }
    lastPrintTime = millis();
  }

  // If a new file has been uploaded, display it
  if (newFileUploaded) {
    ShowGIF(const_cast<char*>(uploadedFilePath.c_str()));
    newFileUploaded = false; // Reset the flag
    playAllMode = false; // Explicitly turn off play-all mode after upload
  }

  // Main playback logic
  if (!currentPlayingFile.isEmpty()) {
    bool framePlayed = gif.playFrame(true, NULL);

    if (playAllMode) {
      // In play-all mode, only change GIF after the timeout, allowing for loops.
      if (millis() - lastPlaybackChange > playbackDuration) {
        Serial.println("Moving to next GIF in play-all mode.");
        playNextFile();
      }
    } else {
      // In single-play mode, restart the same GIF when it finishes.
      if (!framePlayed) {
        Serial.println("Restarting current GIF in single-play mode.");
        gif.open(currentPlayingFile.c_str(), GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDraw);
      }
    }
  }
}
