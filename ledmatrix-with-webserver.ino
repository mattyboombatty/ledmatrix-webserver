/**
 * Project: ESP32-HUB75-GIF-Server (Pro Edition)
 * Description: High-performance GIF player for chained 64x64 HUB75E matrices.
 * Features:
 * - Memory-stable playback (prevents web server hangs).
 * - Snappy "TV-style" transitions with immediate screen clearing.
 * - Integrated Web UI for uploads, management, and real-time diagnostics.
 * - Power consumption estimator and WiFi signal monitor.
 * * Hardware Note: Uses GPIO 32 for the 'E' address line (required for 64-row height).
 */

 #include "FS.h"
 #include <LittleFS.h>
 #include <AnimatedGIF.h>
 #include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
 #include <WiFi.h>
 #include <WebServer.h>
 
 // --- CONFIGURATION ---
 #define PANEL_RES_X 64    // Width of a single module
 #define PANEL_RES_Y 64    // Height of a single module
 #define PANEL_CHAIN 3     // Number of panels daisy-chained
 #define FILESYSTEM LittleFS
 #define FORMAT_LITTLEFS_IF_FAILED true
 
 // WiFi Credentials
 const char* ap_ssid = "Matrix-Sign-AP";
 const char* ap_password = "Password1";
 const char* sta_ssid = "your_ssid";         // Change to your home WiFi
 const char* sta_password = "your_password"; // Change to your home Password
 
 // --- GLOBALS ---
 WebServer server(80);
 MatrixPanel_I2S_DMA *dma_display = nullptr;
 AnimatedGIF gif;
 File f;
 
 int x_offset, y_offset;
 String uploadedFilePath = "";
 bool newFileUploaded = false;
 String currentPlayingFile = "";
 int currentBrightness = 180;
 
 bool playAllMode = false;
 File rootDir;
 unsigned long lastPlaybackChange = 0;
 unsigned long playbackDuration = 15000; // 15 Seconds default
 
 // --- DIAGNOSTICS & POWER ---
 
 /**
  * Estimates current draw in Amps.
  * Calculation: (Pixels * Max Draw * Brightness %) * Duty Cycle
  */
 float estimateCurrent() {
   int totalPixels = (PANEL_RES_X * PANEL_RES_Y * PANEL_CHAIN);
   float brightnessFactor = currentBrightness / 255.0;
   // 0.060A per pixel for full white. Typical GIFs average 30% pixel density.
   return (totalPixels * 0.060) * brightnessFactor * 0.30;
 }
 
 // --- GIF CALLBACKS ---
 
 void GIFDraw(GIFDRAW *pDraw) {
   uint8_t *s;
   uint16_t *usPalette;
   int x, y, iWidth;
 
   iWidth = pDraw->iWidth;
   if (iWidth > dma_display->width()) iWidth = dma_display->width();
   usPalette = pDraw->pPalette;
   y = pDraw->iY + pDraw->y + y_offset;
   s = pDraw->pPixels;
 
   if (pDraw->ucDisposalMethod == 2) {
     for (x=0; x<iWidth; x++) {
       if (s[x] == pDraw->ucTransparent) s[x] = pDraw->ucBackground;
     }
   }
 
   if (pDraw->ucHasTransparency) {
     uint8_t ucTransparent = pDraw->ucTransparent;
     for (x = 0; x < iWidth; x++) {
       if (s[x] != ucTransparent) {
         dma_display->drawPixel(pDraw->iX + x + x_offset, y, usPalette[s[x]]);
       }
     }
   } else {
     for (x=0; x<iWidth; x++) {
       dma_display->drawPixel(pDraw->iX + x + x_offset, y, usPalette[*s++]);
     }
   }
 }
 
 void * GIFOpenFile(const char *fname, int32_t *pSize) {
   f = FILESYSTEM.open(fname);
   if (f) { *pSize = f.size(); return (void *)&f; }
   return NULL;
 }
 
 void GIFCloseFile(void *pHandle) {
   File *f = static_cast<File *>(pHandle);
   if (f != NULL) f->close();
 }
 
 int32_t GIFReadFile(GIFFILE *pFile, uint8_t *pBuf, int32_t iLen) {
     int32_t iBytesRead = iLen;
     File *f = static_cast<File *>(pFile->fHandle);
     if ((pFile->iSize - pFile->iPos) < iLen) iBytesRead = pFile->iSize - pFile->iPos - 1;
     if (iBytesRead <= 0) return 0;
     iBytesRead = (int32_t)f->read(pBuf, iBytesRead);
     pFile->iPos = f->position();
     return iBytesRead;
 }
 
 int32_t GIFSeekFile(GIFFILE *pFile, int32_t iPosition) {
   File *f = static_cast<File *>(pFile->fHandle);
   f->seek(iPosition);
   pFile->iPos = (int32_t)f->position();
   return pFile->iPos;
 }
 
 // --- CORE PLAYBACK CONTROL ---
 
 void ShowGIF(const char *name) {
   dma_display->clearScreen(); // Immediate clear for "snappy" transition
   gif.close();                // FREE MEMORY before opening next
   
   Serial.printf("Opening: %s | RAM: %d\n", name, ESP.getFreeHeap());
 
   if (gif.open(name, GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDraw)) {
     // Center logic for multi-panel chains
     x_offset = (dma_display->width() - gif.getCanvasWidth()) / 2;
     y_offset = (dma_display->height() - gif.getCanvasHeight()) / 2;
     x_offset = max(0, x_offset);
     y_offset = max(0, y_offset);
     
     currentPlayingFile = name;
     lastPlaybackChange = millis();
   } else {
     currentPlayingFile = "";
   }
 }
 
 void playNextFile() {
   File file = rootDir.openNextFile();
   if (!file) {
     rootDir.rewindDirectory();
     file = rootDir.openNextFile();
   }
   if (file) {
     String path = "/" + String(file.name());
     file.close();
     ShowGIF(path.c_str());
   }
 }
 
 // --- WEB HANDLERS ---
 
 void handleRoot() {
   String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'>"
     "<style>body{font-family:sans-serif;background:#222;color:#fff;text-align:center;}"
     ".box{background:#333;padding:15px;border-radius:10px;margin:10px;border:1px solid #444;}"
     "a{color:#00ff00;text-decoration:none;font-weight:bold;}</style></head><body>"
     "<h1>Matrix Controller</h1>"
     "<div class='box'><b>Now Playing:</b><br>" + currentPlayingFile + "</div>"
     "<form method='POST' action='/upload' enctype='multipart/form-data'><input type='file' name='file'><input type='submit' value='Upload'></form>"
     "<div class='box'><a href='/playall'>START PLAYLIST</a> | <a href='/list'>MANAGE FILES</a> | <a href='/diag'>DIAGNOSTICS</a></div>"
     "Brightness: <input type='range' min='0' max='255' value='"+String(currentBrightness)+"' onchange='fetch(\"/brightness?value=\"+this.value)'>"
     "</body></html>";
   server.send(200, "text/html", html);
 }
 
 void handleDiag() {
   int rssi = WiFi.RSSI();
   String html = "<html><body style='background:#111;color:#0f0;font-family:monospace;padding:20px;'>"
     "<h2>SYSTEM DIAGNOSTICS</h2>"
     "<hr>WiFi Signal: " + String(rssi) + " dBm<br>"
     "Est. Power: " + String(estimateCurrent(), 2) + " Amps<br>"
     "Free Heap: " + String(ESP.getFreeHeap()) + " bytes<br>"
     "Uptime: " + String(millis()/60000) + " min<br>"
     "<hr><a href='/' style='color:#fff'><- BACK</a></body></html>";
   server.send(200, "text/html", html);
 }
 
 void handleFileList() {
   String html = "<body style='background:#222;color:#fff;'><h3>File Manager</h3><ul>";
   File root = LittleFS.open("/");
   File file = root.openNextFile();
   while(file){
     html += "<li>" + String(file.name()) + " <a href='/play?file=" + String(file.name()) + "' style='color:#0f0'>[PLAY]</a> "
             "<a href='/delete?file=" + String(file.name()) + "' style='color:#f00'>[DEL]</a></li>";
     file = root.openNextFile();
   }
   html += "</ul><a href='/'>Back</a></body>";
   server.send(200, "text/html", html);
 }
 
 void handlePlay() { if (server.hasArg("file")) { ShowGIF(("/" + server.arg("file")).c_str()); } server.sendHeader("Location", "/"); server.send(302); }
 void handlePlayAll() { playAllMode = true; rootDir.rewindDirectory(); playNextFile(); server.sendHeader("Location", "/"); server.send(302); }
 void handleBrightness() { if (server.hasArg("value")) { currentBrightness = server.arg("value").toInt(); dma_display->setBrightness8(currentBrightness); } server.send(200); }
 void handleDelete() { if (server.hasArg("file")) { LittleFS.remove("/" + server.arg("file")); } server.sendHeader("Location", "/list"); server.send(302); }
 
 void handleUpload() {
   HTTPUpload& upload = server.upload();
   if (upload.status == UPLOAD_FILE_START) {
     f = LittleFS.open("/" + upload.filename, FILE_WRITE);
   } else if (upload.status == UPLOAD_FILE_WRITE) {
     if (f) f.write(upload.buf, upload.currentSize);
   } else if (upload.status == UPLOAD_FILE_END) {
     if (f) f.close();
     newFileUploaded = true;
   }
 }
 
 // --- SYSTEM ---
 
 void setup() {
   Serial.begin(115200);
   LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED);
 
   WiFi.mode(WIFI_AP_STA);
   WiFi.softAP(ap_ssid, ap_password);
   WiFi.begin(sta_ssid, sta_password);
 
   server.on("/", handleRoot);
   server.on("/diag", handleDiag);
   server.on("/list", handleFileList);
   server.on("/play", handlePlay);
   server.on("/playall", handlePlayAll);
   server.on("/delete", handleDelete);
   server.on("/brightness", handleBrightness);
   server.on("/upload", HTTP_POST, [](){ server.send(200); }, handleUpload);
   server.begin();
 
   HUB75_I2S_CFG mxconfig(PANEL_RES_X, PANEL_RES_Y, PANEL_CHAIN);
   mxconfig.driver = HUB75_I2S_CFG::FM6126A; // Common for high-res panels
 
   // GPIO Mapping (Verified for ESP32)
   mxconfig.gpio.r1 = 26; mxconfig.gpio.g1 = 27; mxconfig.gpio.b1 = 25;
   mxconfig.gpio.r2 = 12; mxconfig.gpio.g2 = 13; mxconfig.gpio.b2 = 14;
   mxconfig.gpio.a = 23;  mxconfig.gpio.b = 19;  mxconfig.gpio.c = 5;
   mxconfig.gpio.d = 17;  
   
   // THE MAGIC 'E' PIN (Pin 12 on HUB75 connector)
   // Required for 64-row panels (HUB75E)
   mxconfig.gpio.e = 32; 
 
   mxconfig.gpio.lat = 4; mxconfig.gpio.oe = 15; mxconfig.gpio.clk = 16;
 
   dma_display = new MatrixPanel_I2S_DMA(mxconfig);
   dma_display->begin();
   dma_display->setBrightness8(currentBrightness);
   dma_display->clearScreen();
 
   gif.begin(LITTLE_ENDIAN_PIXELS);
   rootDir = LittleFS.open("/");
   handlePlayAll(); // Start playlist on boot
 }
 
 void loop() {
   server.handleClient();
 
   if (newFileUploaded) {
     ShowGIF(uploadedFilePath.c_str());
     newFileUploaded = false;
     playAllMode = false;
   }
 
   if (!currentPlayingFile.isEmpty()) {
     // gif.playFrame returns false at end of file
     if (!gif.playFrame(true, NULL)) {
       if (playAllMode && (millis() - lastPlaybackChange > playbackDuration)) {
         playNextFile();
       } else {
         // FAST RESTART (Saves memory, prevents leaks)
         gif.seekAnimation(0);
       }
     }
   }
 }