#include "FS.h"
#include <LittleFS.h>
#include <AnimatedGIF.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

// Internal CPU Temp Sensor
#ifdef __cplusplus
extern "C" {
#endif
uint8_t temprature_sens_read();
#ifdef __cplusplus
}
#endif

/* --- CONFIGURATION --- */
#define FILESYSTEM LittleFS
#define FORMAT_LITTLEFS_IF_FAILED true
#define PANEL_RES_X 64 
#define PANEL_RES_Y 64 

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
int currentBrightness = 20;  
bool useTimeMode = false;    
int maxLoops = 1;            
int maxDuration = 10;        
int loopCount = 0;
unsigned long gifStartTime = 0;
int x_offset, y_offset;
unsigned long nextFrameTime = 0;

/* --- MATRIX TEXT HELPERS --- */
void showStatusOnMatrix(String line1, String line2) {
  dma_display->setTextSize(1);
  dma_display->setTextWrap(false);
  int scrollWidth1 = line1.length() * 6;
  int scrollWidth2 = line2.length() * 6;
  int maxScroll = max(scrollWidth1, scrollWidth2) + PANEL_RES_X;

  for (int x = PANEL_RES_X; x > -maxScroll; x--) {
    dma_display->clearScreen();
    dma_display->setTextColor(dma_display->color565(0, 255, 136)); 
    dma_display->setCursor(x, 15);
    dma_display->print(line1);
    dma_display->setTextColor(dma_display->color565(0, 170, 255)); 
    dma_display->setCursor(x, 40);
    dma_display->print(line2);
    delay(25); 
    server.handleClient(); 
  }
  dma_display->clearScreen();
}

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
    for (x = 0; x < iWidth; x++) { 
      if (s[x] != ucTransparent) dma_display->drawPixel(pDraw->iX + x + x_offset, y, usPalette[s[x]]); 
    }
  } else {
    for (x=0; x<iWidth; x++) dma_display->drawPixel(pDraw->iX + x + x_offset, y, usPalette[*s++]);
  }
}

void * GIFOpenFile(const char *fname, int32_t *pSize) {
  f = FILESYSTEM.open(fname, "r");
  if (f) { *pSize = (int32_t)f.size(); return (void *)&f; }
  return NULL;
}
void GIFCloseFile(void *pHandle) { File *f_ptr = static_cast<File *>(pHandle); if (f_ptr != NULL) f_ptr->close(); }
int32_t GIFReadFile(GIFFILE *pFile, uint8_t *pBuf, int32_t iLen) {
    int32_t iBytesRead = iLen;
    File *f_ptr = static_cast<File *>(pFile->fHandle);
    if ((pFile->iSize - pFile->iPos) < iLen) iBytesRead = pFile->iSize - pFile->iPos;
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
  if (gif.open(name, GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDraw)) {
    dma_display->clearScreen(); 
    x_offset = max(0, (int)(dma_display->width() - gif.getCanvasWidth())/2);
    y_offset = max(0, (int)(dma_display->height() - gif.getCanvasHeight())/2);
    currentPlayingFile = name;
    loopCount = 0;
    gifStartTime = millis(); 
    nextFrameTime = millis();
  }
}

void playNextFile() {
  static File root;
  if (!root) root = LittleFS.open("/");
  File nextFile = root.openNextFile();
  if (!nextFile) { root.rewindDirectory(); nextFile = root.openNextFile(); }
  if (nextFile) {
    String path = "/" + String(nextFile.name());
    nextFile.close();
    if (path.endsWith(".gif") || path.endsWith(".GIF")) ShowGIF(path.c_str());
    else playNextFile(); 
  }
}

/* --- WEB SERVER HANDLERS --- */
void handleRoot() {
  String fileName = currentPlayingFile.length() > 1 ? currentPlayingFile.substring(1) : "None";
  String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>:root{--p:#0f8;--b:#121212;--c:#1e1e1e;}body{font-family:sans-serif;background:var(--b);color:#fff;padding:20px;display:flex;flex-direction:column;align-items:center;}";
  html += ".con{width:100%;max-width:400px;}.card{background:var(--c);padding:20px;border-radius:15px;margin-bottom:15px;border:1px solid #333;text-align:center;}";
  html += "h1{color:var(--p);text-align:center;}.sl{color:#888;font-size:0.8rem;text-transform:uppercase;display:flex;justify-content:space-between;align-items:center;}";
  html += ".num-in{background:#333;border:1px solid #444;color:var(--p);width:50px;text-align:center;border-radius:5px;font-weight:bold;padding:4px;}";
  html += ".cf{font-size:1.2rem;color:var(--p);font-weight:bold;margin:10px 0;}";
  html += ".preview{width:100px;height:100px;background:#000;border:2px solid var(--p);border-radius:10px;margin-bottom:15px;object-fit:contain;}";
  html += ".bg{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-top:15px;}a,button{background:#333;color:#fff;text-decoration:none;padding:12px;border-radius:8px;border:none;text-align:center;font-weight:bold;cursor:pointer;}";
  html += ".bp{background:var(--p);color:#000;}input[type='range']{width:100%;accent-color:var(--p);margin:10px 0;}.tgl{display:flex;gap:10px;margin-bottom:15px;}.tgl button{flex:1;font-size:0.7rem;}</style></head><body>";
  
  html += "<div class='con'><h1>Matrix Dash</h1><div class='card'><div class='sl'><span>Live Preview</span></div>";
  if (currentPlayingFile != "") html += "<img src='/stream?file=" + currentPlayingFile + "' class='preview'>";
  html += "<div class='sl'><span>Now Playing</span></div><div class='cf'>" + fileName + "</div>";
  html += "<div class='bg'><a href='/list'>Files</a><a href='/diag'>Settings</a></div></div>";
  
  html += "<div class='card'><div class='sl'><span>Brightness</span><input type='number' id='bn' class='num-in' value='" + String(currentBrightness) + "' oninput='sync(this,\"bs\",\"/brightness\")'></div>";
  html += "<input type='range' id='bs' min='0' max='255' value='" + String(currentBrightness) + "' oninput='sync(this,\"bn\",\"/brightness\")'>";
  html += "<div class='sl' style='margin-bottom:5px;'><span>Switching Mode</span></div><div class='tgl'>";
  html += "<button onclick='setMode(0)' class='" + String(!useTimeMode ? "bp":"") + "'>Loops</button>";
  html += "<button onclick='setMode(1)' class='" + String(useTimeMode ? "bp":"") + "'>Time</button></div>";

  if(!useTimeMode){
    html += "<div class='sl'><span>Max Loops</span><input type='number' id='ln' class='num-in' value='" + String(maxLoops) + "' oninput='sync(this,\"ls\",\"/loops\")'></div>";
    html += "<input type='range' id='ls' min='1' max='50' value='" + String(maxLoops) + "' oninput='sync(this,\"ln\",\"/loops\")'>";
  } else {
    html += "<div class='sl'><span>Seconds Per GIF</span><input type='number' id='tn' class='num-in' value='" + String(maxDuration) + "' oninput='sync(this,\"ts\",\"/duration\")'></div>";
    html += "<input type='range' id='ts' min='1' max='120' value='" + String(maxDuration) + "' oninput='sync(this,\"tn\",\"/duration\")'>";
  }
  html += "</div>";

  html += "<div class='card' style='text-align:center'><form method='POST' action='/upload' enctype='multipart/form-data'><label style='cursor:pointer;color:var(--p);'><strong>+ Upload GIF</strong>";
  html += "<input type='file' name='upload' style='display:none' onchange='this.form.submit()'></label></form></div>";
  html += "<button onclick='location.href=\"/playall\"' class='bp' style='width:100%'>Auto-Playlist</button></div>";
  
  html += "<script>function sync(el,targetId,route){const val=el.value;document.getElementById(targetId).value=val;fetch(route+'?value='+val);}";
  html += "function setMode(m){fetch('/setmode?value='+m).then(()=>location.reload());}</script></body></html>";
  server.send(200, "text/html", html);
}

void handleDiag() {
  float temp_c = (temprature_sens_read() - 32) / 1.8;
  uint32_t freeHeap = ESP.getFreeHeap();
  
  // Storage Calculations
  size_t totalBytes = LittleFS.totalBytes();
  size_t usedBytes = LittleFS.usedBytes();
  size_t freeBytes = totalBytes - usedBytes;

  String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'><style>body{background:#121212;color:#eee;font-family:sans-serif;padding:20px;}";
  html += ".card{background:#1e1e1e;padding:15px;border-radius:12px;border:1px solid #333;margin-bottom:15px;}input,select{background:#333;border:1px solid #444;color:#fff;padding:12px;width:100%;margin:10px 0;border-radius:8px;}";
  html += "button{background:#0f8;color:#000;padding:12px;border:none;border-radius:8px;font-weight:bold;width:100%;cursor:pointer;} .prog{background:#333;border-radius:10px;height:10px;margin:10px 0;} .bar{background:#0f8;height:100%;border-radius:10px;}</style></head><body>";
  
  html += "<h2>Settings</h2><div class='card'><h3>System Stats</h3><p>CPU Temp: " + String(temp_c,1) + "C</p><p>Free RAM: " + String(freeHeap/1024) + "KB</p></div>";
  
  // Storage Section
  float usedPct = ((float)usedBytes / (float)totalBytes) * 100;
  html += "<div class='card'><h3>Storage</h3><p>Used: " + String(usedBytes/1024) + " KB / " + String(totalBytes/1024) + " KB</p>";
  html += "<div class='prog'><div class='bar' style='width:" + String(usedPct) + "%'></div></div><p style='font-size:0.8rem;color:#888;'>" + String(freeBytes/1024) + " KB Free</p></div>";

  html += "<div class='card'><h3>WiFi Setup</h3><form action='/setwifi' method='POST'>";
  html += "<button type='button' id='scanBtn' onclick='startScan()'>Scan for Networks</button>";
  html += "<select name='s' id='ssidSelect'><option>Select Network...</option></select>";
  html += "<input name='p' type='password' placeholder='Password'><button type='submit' style='background:#0f8'>Save & Reboot</button></form></div>";
  html += "<button onclick='fetch(\"/reboot\")' style='background:#f44;color:#fff;'>Restart</button><br><br><a href='/' style='color:#0f8;'>Back</a>";
  
  html += "<script>function startScan(){ const btn = document.getElementById('scanBtn'); const sel = document.getElementById('ssidSelect'); btn.innerText='Scanning...'; btn.disabled=true;";
  html += "fetch('/scan').then(r=>r.text()).then(data=>{ sel.innerHTML = data; btn.innerText='Scan Complete'; btn.disabled=false; }); }</script></body></html>";
  
  server.send(200, "text/html", html);
}

void handleFileList() {
  String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'><style>body{background:#121212;color:#eee;font-family:sans-serif;padding:20px;}table{width:100%;border-collapse:collapse;}td,th{padding:10px;border-bottom:1px solid #333;text-align:left;}a{color:#0f8;text-decoration:none;}</style></head><body>";
  html += "<h2>Gallery</h2><table><tr><th>File</th><th>Size</th><th>Action</th></tr>";
  File r = LittleFS.open("/"); File file = r.openNextFile();
  while(file){ 
    String fn = String(file.name()); 
    if(fn.endsWith(".gif") || fn.endsWith(".GIF")){
      String fsize = String(file.size() / 1024) + " KB";
      html += "<tr><td>" + fn + "</td><td>" + fsize + "</td><td><a href='/play?file=" + fn + "'>Play</a> | <a href='/delete?file=" + fn + "' style='color:#f44;'>Del</a></td></tr>"; 
    }
    file = r.openNextFile(); 
  }
  html += "</table><br><a href='/'>Back</a></body></html>";
  server.send(200, "text/html", html);
}

// REST OF THE HANDLERS (Scan, Stream, Play, etc.) UNCHANGED
void handleScan() {
  int n = WiFi.scanNetworks();
  String options = "";
  if (n == 0) options = "<option>No networks found</option>";
  else {
    for (int i = 0; i < n; ++i) {
      options += "<option value='" + WiFi.SSID(i) + "'>" + WiFi.SSID(i) + " (" + String(WiFi.RSSI(i)) + "dBm)</option>";
    }
  }
  server.send(200, "text/html", options);
}

void handleStream() {
  if (server.hasArg("file")) {
    String path = server.arg("file");
    if (LittleFS.exists(path)) {
      File file = LittleFS.open(path, "r");
      server.streamFile(file, "image/gif");
      file.close();
      return;
    }
  }
  server.send(404);
}

void handleSetWiFi() { if (server.hasArg("s") && server.hasArg("p")) { prefs.begin("wifi", false); prefs.putString("ssid", server.arg("s")); prefs.putString("pass", server.arg("p")); prefs.end(); server.send(200, "WiFi Saved. Rebooting..."); delay(2000); ESP.restart(); } }
void handleSetMode() { if (server.hasArg("value")) { useTimeMode = server.arg("value").toInt(); prefs.begin("settings", false); prefs.putBool("useTime", useTimeMode); prefs.end(); } server.send(200); }
void handleDuration() { if (server.hasArg("value")) { maxDuration = server.arg("value").toInt(); prefs.begin("settings", false); prefs.putInt("dur", maxDuration); prefs.end(); } server.send(200); }
void handleBrightness() { if (server.hasArg("value")) { currentBrightness = server.arg("value").toInt(); dma_display->setBrightness8(currentBrightness); prefs.begin("settings", false); prefs.putInt("bright", currentBrightness); prefs.end(); } server.send(200); }
void handleLoops() { if (server.hasArg("value")) { maxLoops = server.arg("value").toInt(); prefs.begin("settings", false); prefs.putInt("loops", maxLoops); prefs.end(); } server.send(200); }
void handleReboot() { server.send(200); delay(1000); ESP.restart(); }
void handlePlay() { if (server.hasArg("file")) { uploadedFilePath = "/" + server.arg("file"); newFileUploaded = true; playAllMode = false; } server.sendHeader("Location", "/"); server.send(302); }
void handlePlayAll() { playAllMode = true; playNextFile(); server.sendHeader("Location", "/"); server.send(302); }
void handleDelete() { if (server.hasArg("file")) LittleFS.remove("/" + server.arg("file")); server.sendHeader("Location", "/list"); server.send(302); }
void handleUpload() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) f = LittleFS.open("/" + upload.filename, FILE_WRITE);
  else if (upload.status == UPLOAD_FILE_WRITE && f) f.write(upload.buf, upload.currentSize);
  else if (upload.status == UPLOAD_FILE_END) { if (f) f.close(); uploadedFilePath = "/" + upload.filename; newFileUploaded = true; playAllMode = false; server.sendHeader("Location", "/list"); server.send(303); }
}

void setup() {
  Serial.begin(115200);
  LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED);
  
  prefs.begin("wifi", true);
  String ssid = prefs.getString("ssid", "");
  String pass = prefs.getString("pass", "");
  prefs.end();

  prefs.begin("settings", true);
  currentBrightness = prefs.getInt("bright", 20);
  maxLoops = prefs.getInt("loops", 1);
  maxDuration = prefs.getInt("dur", 10);
  useTimeMode = prefs.getBool("useTime", false);
  prefs.end();

  HUB75_I2S_CFG mxconfig(PANEL_RES_X, PANEL_RES_Y, 1);
  mxconfig.gpio.r1 = 26; mxconfig.gpio.g1 = 27; mxconfig.gpio.b1 = 25;
  mxconfig.gpio.r2 = 12; mxconfig.gpio.g2 = 13; mxconfig.gpio.b2 = 14;
  mxconfig.gpio.a = 23;  mxconfig.gpio.b = 19;  mxconfig.gpio.c = 5;
  mxconfig.gpio.d = 17;  mxconfig.gpio.e = 32;
  mxconfig.gpio.lat = 4; mxconfig.gpio.oe = 15; mxconfig.gpio.clk = 16;
  mxconfig.driver = HUB75_I2S_CFG::FM6126A;

  dma_display = new MatrixPanel_I2S_DMA(mxconfig);
  dma_display->begin();
  dma_display->setBrightness8(currentBrightness);

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(ap_ssid, ap_password);
  
  if (ssid != "") {
    WiFi.begin(ssid.c_str(), pass.c_str());
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 8000) { delay(500); }
  }

  server.on("/", handleRoot);
  server.on("/diag", handleDiag);
  server.on("/list", handleFileList);
  server.on("/stream", handleStream);
  server.on("/scan", handleScan);
  server.on("/setwifi", HTTP_POST, handleSetWiFi);
  server.on("/setmode", handleSetMode);
  server.on("/duration", handleDuration);
  server.on("/brightness", handleBrightness);
  server.on("/loops", handleLoops);
  server.on("/play", handlePlay);
  server.on("/playall", handlePlayAll);
  server.on("/delete", handleDelete);
  server.on("/reboot", handleReboot);
  server.on("/upload", HTTP_POST, [](){}, handleUpload);
  server.begin();

  gif.begin(LITTLE_ENDIAN_PIXELS);
  playNextFile(); 
}

void loop() {
  server.handleClient();
  if (newFileUploaded) { ShowGIF(uploadedFilePath.c_str()); newFileUploaded = false; }
  
  if (!currentPlayingFile.isEmpty() && millis() >= nextFrameTime) {
    int iDelay = 0;
    int res = gif.playFrame(false, &iDelay); 
    if (res == 1) { nextFrameTime = millis() + iDelay; } 
    else if (res == 0) { 
      loopCount++;
      bool shouldAdvance = false;
      if (playAllMode) {
        if (useTimeMode) {
          if ((millis() - gifStartTime) / 1000 >= maxDuration) shouldAdvance = true;
          else gif.reset(); 
        } else {
          if (loopCount >= maxLoops) shouldAdvance = true;
          else gif.reset();
        }
      } else { gif.reset(); }
      if (shouldAdvance) playNextFile();
      else nextFrameTime = millis();
    }
  }
}