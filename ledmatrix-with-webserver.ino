#include "FS.h"
#include <LittleFS.h>
#include <AnimatedGIF.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <WiFi.h>
#include <WebServer.h>

/* --- CONFIGURATION --- */
#define FILESYSTEM LittleFS
#define FORMAT_LITTLEFS_IF_FAILED true
#define PANEL_RES_X 64 
#define PANEL_RES_Y 64 
#define PANEL_CHAIN 1 

const char* ap_ssid = "Matrix-Sign-AP";
const char* ap_password = "Password1";
const char* sta_ssid = "your_ssid";
const char* sta_password = "YourPassword";

/* --- GLOBAL OBJECTS --- */
WebServer server(80);
MatrixPanel_I2S_DMA *dma_display = nullptr;
AnimatedGIF gif;
File f;
File root;

/* --- STATE VARIABLES --- */
String currentPlayingFile = "";
String uploadedFilePath = "";
bool newFileUploaded = false;
bool playAllMode = true; 
int currentBrightness = 180;
int x_offset, y_offset;
unsigned long lastPlaybackChange = 0;
unsigned long playbackDuration = 15000; 

/* --- GIF CALLBACKS --- */
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
      if (s[x] != ucTransparent) dma_display->drawPixel(pDraw->iX + x + x_offset, y, usPalette[s[x]]);
    }
  } else {
    for (x=0; x<iWidth; x++) dma_display->drawPixel(pDraw->iX + x + x_offset, y, usPalette[*s++]);
  }
}

void * GIFOpenFile(const char *fname, int32_t *pSize) {
  f = FILESYSTEM.open(fname);
  if (f) { *pSize = (int32_t)f.size(); return (void *)&f; }
  return NULL;
}

void GIFCloseFile(void *pHandle) {
  File *f_ptr = static_cast<File *>(pHandle);
  if (f_ptr != NULL) f_ptr->close();
}

int32_t GIFReadFile(GIFFILE *pFile, uint8_t *pBuf, int32_t iLen) {
    int32_t iBytesRead = iLen;
    File *f_ptr = static_cast<File *>(pFile->fHandle);
    if ((pFile->iSize - pFile->iPos) < iLen) iBytesRead = pFile->iSize - pFile->iPos - 1;
    if (iBytesRead <= 0) return 0;
    iBytesRead = (int32_t)f_ptr->read(pBuf, iBytesRead);
    pFile->iPos = (int32_t)f_ptr->position();
    return iBytesRead;
}

int32_t GIFSeekFile(GIFFILE *pFile, int32_t iPosition) {
  File *f_ptr = static_cast<File *>(pFile->fHandle);
  f_ptr->seek(iPosition);
  pFile->iPos = (int32_t)f_ptr->position();
  return pFile->iPos;
}

/* --- PLAYBACK ENGINE --- */
void ShowGIF(const char *name) {
  if (currentPlayingFile != "") gif.close();
  dma_display->clearScreen();
  if (gif.open(name, GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDraw)) {
    x_offset = max(0, (int)(dma_display->width() - gif.getCanvasWidth())/2);
    y_offset = max(0, (int)(dma_display->height() - gif.getCanvasHeight())/2);
    currentPlayingFile = name;
    lastPlaybackChange = millis();
  }
}

void playNextFile() {
  File nextFile = root.openNextFile();
  if (!nextFile) { 
    root.rewindDirectory(); 
    nextFile = root.openNextFile(); 
  }
  if (nextFile) {
    String path = "/" + String(nextFile.name());
    nextFile.close();
    ShowGIF(path.c_str());
  }
}

/* --- WEB SERVER HANDLERS --- */
void handleRoot() {
  String status = playAllMode ? "Auto-Playlist" : "Single Loop";
  String fileName = currentPlayingFile.length() > 1 ? currentPlayingFile.substring(1) : "None";
  
  String html = "<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>"
    "<style>body{font-family:sans-serif;background:#1a1a1a;color:#eee;text-align:center;padding:20px;}"
    ".card{background:#333;padding:15px;border-radius:10px;margin:10px 0;border-bottom:4px solid #0f0;}"
    "a{color:#0f0;text-decoration:none;font-weight:bold;padding:12px;display:inline-block; border:1px solid #444; border-radius:5px; margin:5px;}"
    "input[type='range']{width:80%; margin:20px 0;}"
    "input[type='number']{background:#333; color:#0f0; border:1px solid #555; padding:5px; border-radius:5px; width:60px; font-size:1.1em;}"
    "</style></head><body>"
    "<h1>Matrix Dash</h1>"
    "<div class='card'><strong>NOW PLAYING:</strong><br><span style='font-size:1.2em;'>" + fileName + "</span><br><small>" + status + "</small></div>"
    "<div><a href='/list'>Manage Files</a><a href='/diag'>Diagnostics</a></div>"
    "<form method='POST' action='/upload' enctype='multipart/form-data' style='margin-top:20px;'>"
    "<input type='file' name='file'><br><br><input type='submit' value='Upload GIF' style='background:#0f0; border:none; padding:10px 20px; border-radius:5px; cursor:pointer; font-weight:bold;'>"
    "</form>"
    "<div class='card'><h3>Brightness Control</h3>"
    "<input type='range' id='s' min='0' max='255' value='"+String(currentBrightness)+"' oninput='updateB(this.value)'>"
    "<br><input type='number' id='n' min='0' max='255' value='"+String(currentBrightness)+"' oninput='updateB(this.value)'>"
    "</div>"
    "<script>function updateB(v){document.getElementById(\"s\").value=v;document.getElementById(\"n\").value=v;fetch(\"/brightness?value=\"+v);}</script>"
    "</body></html>";
  server.send(200, "text/html", html);
}

void handleDiag() {
  uint32_t freeRAM = ESP.getFreeHeap();
  uint32_t totalRAM = ESP.getHeapSize();
  float ramPressure = 100.0 - ((float)freeRAM / totalRAM * 100.0);
  
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><style>body{background:#000;color:#0f0;font-family:monospace;padding:20px;} a{color:#fff;}</style></head><body>"
    "<h2>SYSTEM STATS</h2><hr>"
    "WiFi Signal: " + String(WiFi.RSSI()) + " dBm<br>"
    "RAM Pressure: " + String(ramPressure, 1) + "%<br>"
    "Free Heap: " + String(freeRAM/1024) + " KB<br>"
    "Storage Used: " + String(LittleFS.usedBytes()/1024) + " KB<br>"
    "Uptime: " + String(millis()/60000) + " min<br><br>"
    "<a href='/'>[ Back to Dash ]</a>"
    "<script>setTimeout(function(){location.reload();}, 3000);</script></body></html>";
  server.send(200, "text/html", html);
}

void handleFileList() {
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'><style>body{font-family:sans-serif;background:#222;color:#fff;text-align:center;} li{background:#333;margin:5px;padding:10px;list-style:none;display:flex;justify-content:space-between;align-items:center;} a{text-decoration:none; padding:5px 10px; border-radius:4px;}</style></head><body>"
    "<h2>Stored Files</h2><a href='/playall' style='color:#0f0; border:1px solid #0f0;'>PLAY ALL</a><br><ul style='padding:10px;'>";
  File r = LittleFS.open("/");
  File file = r.openNextFile();
  while(file){
    String fn = String(file.name());
    html += "<li>" + fn + " <div><a href='/play?file=" + fn + "' style='background:#0f0; color:#000;'>Play</a> <a href='/delete?file=" + fn + "' style='background:#f00; color:#fff; margin-left:5px;'>Del</a></div></li>";
    file = r.openNextFile();
  }
  html += "</ul><br><a href='/' style='color:#eee;'>Back</a></body></html>";
  server.send(200, "text/html", html);
}

void handlePlay() { if (server.hasArg("file")) { uploadedFilePath = "/" + server.arg("file"); newFileUploaded = true; playAllMode = false; } server.sendHeader("Location", "/"); server.send(302); }
void handlePlayAll() { playAllMode = true; playNextFile(); server.sendHeader("Location", "/"); server.send(302); }
void handleBrightness() { if (server.hasArg("value")) { currentBrightness = server.arg("value").toInt(); dma_display->setBrightness8(currentBrightness); } server.send(200); }
void handleDuration() { if (server.hasArg("value")) playbackDuration = server.arg("value").toInt() * 1000; server.send(200); }
void handleDelete() { if (server.hasArg("file")) LittleFS.remove("/" + server.arg("file")); server.sendHeader("Location", "/list"); server.send(302); }

void handleUpload() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) { 
    dma_display->clearScreen(); 
    f = LittleFS.open("/" + upload.filename, FILE_WRITE); 
  }
  else if (upload.status == UPLOAD_FILE_WRITE && f) {
    f.write(upload.buf, upload.currentSize);
    yield();
  }
  else if (upload.status == UPLOAD_FILE_END) { 
    if (f) f.close(); 
    uploadedFilePath = "/" + upload.filename; 
    newFileUploaded = true; 
    playAllMode = false; 
  }
}

/* --- ARDUINO CORE --- */
void setup() {
  Serial.begin(115200);
  LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED);
  
  WiFi.mode(WIFI_AP_STA);
  IPAddress ip(192, 168, 42, 1);
  WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0));
  WiFi.softAP(ap_ssid, ap_password);
  WiFi.begin(sta_ssid, sta_password);

  server.on("/", handleRoot);
  server.on("/diag", handleDiag);
  server.on("/list", handleFileList);
  server.on("/play", handlePlay);
  server.on("/playall", handlePlayAll);
  server.on("/brightness", handleBrightness);
  server.on("/duration", handleDuration);
  server.on("/delete", handleDelete);
  server.on("/upload", HTTP_POST, [](){ server.send(200); }, handleUpload);
  server.begin();

  HUB75_I2S_CFG mxconfig(PANEL_RES_X, PANEL_RES_Y, PANEL_CHAIN);
  mxconfig.gpio.r1 = 26; mxconfig.gpio.g1 = 27; mxconfig.gpio.b1 = 25;
  mxconfig.gpio.r2 = 12; mxconfig.gpio.g2 = 13; mxconfig.gpio.b2 = 14;
  mxconfig.gpio.a = 23;  mxconfig.gpio.b = 19;  mxconfig.gpio.c = 5;
  mxconfig.gpio.d = 17;  mxconfig.gpio.e = 32;
  mxconfig.gpio.lat = 4; mxconfig.gpio.oe = 15; mxconfig.gpio.clk = 16;
  mxconfig.driver = HUB75_I2S_CFG::FM6126A;

  dma_display = new MatrixPanel_I2S_DMA(mxconfig);
  dma_display->begin();
  dma_display->setBrightness8(currentBrightness);
  
  gif.begin(LITTLE_ENDIAN_PIXELS);
  root = LittleFS.open("/");
  playNextFile(); 
}

void loop() {
  server.handleClient();
  if (newFileUploaded) { ShowGIF(uploadedFilePath.c_str()); newFileUploaded = false; }

  if (!currentPlayingFile.isEmpty()) {
    bool framePlayed = gif.playFrame(true, NULL);
    if (playAllMode && (millis() - lastPlaybackChange > playbackDuration)) {
      playNextFile();
    } else if (!playAllMode && !framePlayed) {
      gif.reset(); 
    }
  }
}