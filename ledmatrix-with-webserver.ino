#include "FS.h"
#include <LittleFS.h>
#include <AnimatedGIF.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

/* --- CONFIGURATION --- */
#define FILESYSTEM LittleFS
#define FORMAT_LITTLEFS_IF_FAILED true
#define PANEL_RES_X 64 
#define PANEL_RES_Y 64 
#define PANEL_CHAIN 1 

const char* ap_ssid = "Matrix-Sign-AP";
const char* ap_password = "Password1";

/* --- GLOBAL OBJECTS --- */
WebServer server(80);
Preferences prefs;
MatrixPanel_I2S_DMA *dma_display = nullptr;
AnimatedGIF gif;
File f;

/* --- STATE VARIABLES --- */
String currentPlayingFile = "";
String uploadedFilePath = "";
bool newFileUploaded = false;
bool playAllMode = true; 
int currentBrightness = 3;  
int loopCount = 0;
int maxLoops = 1;           
int x_offset, y_offset;

/* --- GIF CALLBACKS --- */
void GIFDraw(GIFDRAW *pDraw) {
  uint8_t *s; uint16_t *usPalette; int x, y, iWidth;
  iWidth = pDraw->iWidth;
  if (iWidth > dma_display->width()) iWidth = dma_display->width();
  usPalette = pDraw->pPalette;
  y = pDraw->iY + pDraw->y + y_offset; 
  s = pDraw->pPixels;
  if (pDraw->ucDisposalMethod == 2) {
    for (x=0; x<iWidth; x++) { if (s[x] == pDraw->ucTransparent) s[x] = pDraw->ucBackground; }
  }
  if (pDraw->ucHasTransparency) {
    uint8_t ucTransparent = pDraw->ucTransparent;
    for (x = 0; x < iWidth; x++) { if (s[x] != ucTransparent) dma_display->drawPixel(pDraw->iX + x + x_offset, y, usPalette[s[x]]); }
  } else {
    for (x=0; x<iWidth; x++) dma_display->drawPixel(pDraw->iX + x + x_offset, y, usPalette[*s++]);
  }
}

void * GIFOpenFile(const char *fname, int32_t *pSize) {
  f = FILESYSTEM.open(fname);
  if (f) { *pSize = (int32_t)f.size(); return (void *)&f; }
  return NULL;
}

void GIFCloseFile(void *pHandle) { File *f_ptr = static_cast<File *>(pHandle); if (f_ptr != NULL) f_ptr->close(); }

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
    loopCount = 0;
  }
}

void playNextFile() {
  File r = LittleFS.open("/");
  File nextFile = r.openNextFile();
  if (!nextFile) { r.rewindDirectory(); nextFile = r.openNextFile(); }
  if (nextFile) {
    String path = "/" + String(nextFile.name());
    nextFile.close();
    ShowGIF(path.c_str());
  }
}

/* --- WEB SERVER HANDLERS --- */
void handleRoot() {
  String fileName = currentPlayingFile.length() > 1 ? currentPlayingFile.substring(1) : "None";
  String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>:root{--p:#0f8;--b:#121212;--c:#1e1e1e;}body{font-family:sans-serif;background:var(--b);color:#fff;padding:20px;display:flex;flex-direction:column;align-items:center;}";
  html += ".con{width:100%;max-width:400px;}.card{background:var(--c);padding:20px;border-radius:15px;margin-bottom:15px;border:1px solid #333;}";
  html += "h1{color:var(--p);text-align:center;}.sl{color:#888;font-size:0.8rem;text-transform:uppercase;display:flex;justify-content:space-between;align-items:center;}";
  html += ".num-in{background:#333;border:1px solid #444;color:var(--p);width:50px;text-align:center;border-radius:5px;font-weight:bold;padding:4px;}";
  html += ".cf{font-size:1.2rem;color:var(--p);font-weight:bold;margin:10px 0;}";
  html += ".bg{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-top:15px;}a,button{background:#333;color:#fff;text-decoration:none;padding:12px;border-radius:8px;border:none;text-align:center;font-weight:bold;}";
  html += ".bp{background:var(--p);color:#000;}input[type='range']{width:100%;accent-color:var(--p);margin:10px 0;}</style></head><body>";
  
  html += "<div class='con'><h1>Matrix Dash</h1>";
  
  // Now Playing Card
  html += "<div class='card'><div class='sl'><span>Now Playing</span></div><div class='cf'>" + fileName + "</div>";
  html += "<div class='bg'><a href='/list'>Files</a><a href='/diag'>Settings</a></div></div>";
  
  // Controls Card
  html += "<div class='card'>";
  html += "<div class='sl'><span>Brightness</span><input type='number' id='bn' class='num-in' value='" + String(currentBrightness) + "' oninput='sync(this,\"bs\",\"/brightness\")'></div>";
  html += "<input type='range' id='bs' min='0' max='255' value='" + String(currentBrightness) + "' oninput='sync(this,\"bn\",\"/brightness\")'>";
  
  html += "<div class='sl' style='margin-top:10px;'><span>Loops Per GIF</span><input type='number' id='ln' class='num-in' value='" + String(maxLoops) + "' oninput='sync(this,\"ls\",\"/loops\")'></div>";
  html += "<input type='range' id='ls' min='1' max='20' value='" + String(maxLoops) + "' oninput='sync(this,\"ln\",\"/loops\")'></div>";
  
  // Upload Card
  html += "<div class='card' style='text-align:center'><form method='POST' action='/upload' enctype='multipart/form-data'><label style='cursor:pointer;color:var(--p);'><strong>+ Upload GIF</strong>";
  html += "<input type='file' name='upload' style='display:none' onchange='this.form.submit()'></label></form></div>";
  
  html += "<button onclick='location.href=\"/playall\"' class='bp' style='width:100%'>Auto-Playlist</button></div>";
  
  // Sync Script
  html += "<script>function sync(el,targetId,route){const val=el.value;document.getElementById(targetId).value=val;fetch(route+'?value='+val);}</script></body></html>";
  
  server.send(200, "text/html", html);
}

void handleDiag() {
  uint32_t totalBytes = LittleFS.totalBytes();
  uint32_t usedBytes = LittleFS.usedBytes();
  float usagePct = (totalBytes > 0) ? ((float)usedBytes / (float)totalBytes) * 100.0 : 0;

  String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'><style>body{background:#000;color:#0f0;font-family:monospace;padding:20px;}";
  html += "input{background:#111;border:1px solid #0f0;color:#0f0;padding:10px;width:90%;margin:5px 0;}button{background:#0f0;color:#000;padding:10px;border:none;font-weight:bold;width:95%;cursor:pointer;margin-top:10px;}";
  html += ".bb{background:#222;height:12px;width:100%;margin:8px 0;}.bf{background:#007bff;height:100%;}</style></head><body>";
  html += "<h2>SYSTEM & WIFI</h2><hr>";
  html += "Storage: " + String(usedBytes/1024) + "KB / " + String(totalBytes/1024) + "KB";
  html += "<div class='bb'><div class='bf' style='width:" + String(usagePct) + "%'></div></div>";
  html += "<p>WiFi: " + WiFi.SSID() + " (" + String(WiFi.RSSI()) + "dBm)</p>";
  
  html += "<div style='background:#111;padding:15px;border:1px solid #333;'><h3>Update WiFi</h3><form action='/setwifi' method='POST'>";
  html += "SSID:<br><input name='s' type='text'><br>Pass:<br><input name='p' type='password'><br><button type='submit'>Save & Connect</button></form></div>";
  
  html += "<button onclick='if(confirm(\"Restart?\"))fetch(\"/reboot\")' style='background:#f44;color:#fff;'>Restart Device</button>";
  html += "<br><br><a href='/' style='color:#fff;'>&larr; Back to Dash</a></body></html>";
  server.send(200, "text/html", html);
}

void handleSetWiFi() {
  if (server.hasArg("s") && server.hasArg("p")) {
    prefs.begin("wifi", false);
    prefs.putString("ssid", server.arg("s"));
    prefs.putString("pass", server.arg("p"));
    prefs.end();
    server.send(200, "text/plain", "WiFi Saved. Rebooting...");
    delay(2000);
    ESP.restart();
  }
}

void handleReboot() { server.send(200); delay(1000); ESP.restart(); }

void handleFileList() {
  String html = "<!DOCTYPE html><html><head><style>body{font-family:sans-serif;background:#1a1a1a;color:#eee;padding:20px;}table{width:100%;border-collapse:collapse;}td,th{padding:12px;border-bottom:1px solid #333;text-align:left;}.btn{padding:6px 10px;border-radius:4px;text-decoration:none;font-weight:bold;font-size:12px;}.p{background:#0f0;color:#000;}.d{background:#f44;color:#fff;}</style></head><body>";
  html += "<h2>Gallery</h2><table><tr><th>Name</th><th>Size</th><th>Action</th></tr>";
  File r = LittleFS.open("/");
  File file = r.openNextFile();
  while(file){
    String fn = String(file.name());
    String sz = String(file.size()/1024) + "KB";
    html += "<tr><td>" + fn + "</td><td>" + sz + "</td><td><a href='/play?file=" + fn + "' class='btn p'>Play</a>";
    html += " <a href='#' onclick='del(\""+fn+"\")' class='btn d'>X</a></td></tr>";
    file = r.openNextFile();
  }
  html += "</table><br><a href='/' style='color:#0f8;'>Back</a><script>function del(n){if(confirm('Delete '+n+'?'))fetch('/delete?file='+n).then(()=>location.reload());}</script></body></html>";
  server.send(200, "text/html", html);
}

/* --- ACTION HANDLERS --- */
void handlePlay() { if (server.hasArg("file")) { uploadedFilePath = "/" + server.arg("file"); newFileUploaded = true; playAllMode = false; } server.sendHeader("Location", "/"); server.send(302); }
void handlePlayAll() { playAllMode = true; playNextFile(); server.sendHeader("Location", "/"); server.send(302); }
void handleBrightness() { if (server.hasArg("value")) { currentBrightness = server.arg("value").toInt(); dma_display->setBrightness8(currentBrightness); } server.send(200); }
void handleLoops() { if (server.hasArg("value")) { maxLoops = server.arg("value").toInt(); } server.send(200); }
void handleDelete() { if (server.hasArg("file")) LittleFS.remove("/" + server.arg("file")); server.send(200); }

void handleUpload() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) { dma_display->clearScreen(); f = LittleFS.open("/" + upload.filename, FILE_WRITE); }
  else if (upload.status == UPLOAD_FILE_WRITE && f) { f.write(upload.buf, upload.currentSize); }
  else if (upload.status == UPLOAD_FILE_END) { if (f) f.close(); uploadedFilePath = "/" + upload.filename; newFileUploaded = true; playAllMode = false; server.sendHeader("Location", "/list"); server.send(303); }
}

/* --- ARDUINO CORE --- */
void setup() {
  Serial.begin(115200);
  LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED);
  
  prefs.begin("wifi", true);
  String ssid = prefs.getString("ssid", "");
  String pass = prefs.getString("pass", "");
  prefs.end();

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(ap_ssid, ap_password);
  if (ssid != "") WiFi.begin(ssid.c_str(), pass.c_str());

  server.on("/", handleRoot);
  server.on("/diag", handleDiag);
  server.on("/setwifi", HTTP_POST, handleSetWiFi);
  server.on("/reboot", handleReboot);
  server.on("/list", handleFileList);
  server.on("/play", handlePlay);
  server.on("/playall", handlePlayAll);
  server.on("/brightness", handleBrightness);
  server.on("/loops", handleLoops);
  server.on("/delete", handleDelete);
  server.on("/upload", HTTP_POST, [](){}, handleUpload);
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
  playNextFile(); 
}

void loop() {
  server.handleClient();
  if (newFileUploaded) { ShowGIF(uploadedFilePath.c_str()); newFileUploaded = false; }
  if (!currentPlayingFile.isEmpty()) {
    int res = gif.playFrame(true, NULL);
    if (res == 0) { 
      loopCount++;
      if (playAllMode && loopCount >= maxLoops) { playNextFile(); } 
      else { gif.reset(); }
    }
  }
}