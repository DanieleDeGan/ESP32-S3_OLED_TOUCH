#include "net_ota.h"
#include "secrets.h"

#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <WebServer.h>
#include <Update.h>

// ---------------------------------------------------------------------
//  Stato interno
// ---------------------------------------------------------------------
static WebServer        server(80);
static ota_progress_cb_t s_progressCb        = nullptr;
static bool              s_webUpdateAuthorized = false;
static char             s_msg[24];

void net_setOtaProgressCb(ota_progress_cb_t cb) { s_progressCb = cb; }
bool   net_isConnected() { return WiFi.status() == WL_CONNECTED; }
String net_ip()          { return WiFi.localIP().toString(); }

// ---------------------------------------------------------------------
//  Pagina web /  (form di upload, self-contained, nessuna CDN)
// ---------------------------------------------------------------------
static const char UPDATE_PAGE[] PROGMEM = R"HTML(
<!doctype html><html lang="it"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>WSOLED-C3 OTA</title><style>
 body{font-family:system-ui,Arial,sans-serif;background:#111;color:#eee;margin:0;padding:2rem;display:flex;justify-content:center}
 .card{max-width:420px;width:100%;background:#1c1c1c;border:1px solid #333;border-radius:12px;padding:1.5rem}
 h1{font-size:1.05rem;margin:0 0 1rem}
 input[type=file]{width:100%;margin:.5rem 0 1rem;color:#ccc}
 button{width:100%;padding:.7rem;border:0;border-radius:8px;background:#3b82f6;color:#fff;font-size:1rem;cursor:pointer}
 button:disabled{background:#555}
 progress{width:100%;height:1rem;margin-top:1rem}
 .muted{color:#8a8a8a;font-size:.8rem;margin-top:1rem;line-height:1.4}
</style></head><body><div class="card">
 <h1>Aggiornamento firmware &mdash; WSOLED-C3</h1>
 <form id="f"><input type="file" name="update" accept=".bin" required>
 <button type="submit" id="b">Carica e aggiorna</button>
 <progress id="p" value="0" max="100" hidden></progress></form>
 <p class="muted" id="s">Seleziona il .bin generato da Arduino IDE:
 <br>Sketch &gt; Export Compiled Binary, poi prendi il file <code>*.ino.bin</code>.</p>
<script>
const f=document.getElementById('f'),b=document.getElementById('b'),p=document.getElementById('p'),s=document.getElementById('s');
f.addEventListener('submit',e=>{e.preventDefault();const fd=new FormData(f),x=new XMLHttpRequest();
 x.open('POST','/update');p.hidden=false;b.disabled=true;
 x.upload.onprogress=ev=>{if(ev.lengthComputable){const pc=Math.round(ev.loaded/ev.total*100);p.value=pc;s.textContent='Caricamento '+pc+'%';}};
 x.onload=()=>{s.textContent=(x.status==200)?'OK! La scheda si riavvia...':('Errore: '+x.responseText);};
 x.onerror=()=>{s.textContent='Errore di rete';b.disabled=false;};
 x.send(fd);});
</script></div></body></html>
)HTML";

// ---------------------------------------------------------------------
//  Handler web
// ---------------------------------------------------------------------
static bool webAuthOk() {
  if (strlen(WEB_OTA_USER) == 0) return true;      // auth disattivata
  return server.authenticate(WEB_OTA_USER, WEB_OTA_PASS);
}

static void handleRoot() {
  if (!webAuthOk()) { server.requestAuthentication(); return; }
  server.send_P(200, "text/html", UPDATE_PAGE);
}

// Fine dell'upload: risposta e (se ok) reboot.
static void handleUpdateDone() {
  if (!s_webUpdateAuthorized) { server.requestAuthentication(); return; }
  bool ok = !Update.hasError();
  server.sendHeader("Connection", "close");
  server.send(ok ? 200 : 500, "text/plain", ok ? "OK" : "Aggiornamento fallito");
  if (ok) { delay(300); ESP.restart(); }
}

// Ricezione del file a blocchi -> scrittura su partizione OTA.
static void handleUpdateUpload() {
  HTTPUpload& up = server.upload();
  switch (up.status) {
    case UPLOAD_FILE_START:
      s_webUpdateAuthorized = webAuthOk();
      if (!s_webUpdateAuthorized) return;
      Serial.printf("[WebOTA] Start: %s\n", up.filename.c_str());
      if (s_progressCb) s_progressCb(-1, "Web OTA...");
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
      break;

    case UPLOAD_FILE_WRITE:
      if (!s_webUpdateAuthorized) return;
      if (Update.write(up.buf, up.currentSize) != up.currentSize) Update.printError(Serial);
      if (s_progressCb) {
        snprintf(s_msg, sizeof(s_msg), "Web OTA %uKB", (unsigned)(Update.progress() / 1024));
        s_progressCb(-1, s_msg);
      }
      break;

    case UPLOAD_FILE_END:
      if (!s_webUpdateAuthorized) return;
      if (Update.end(true)) {
        Serial.printf("[WebOTA] OK: %u byte\n", up.totalSize);
        if (s_progressCb) s_progressCb(100, "Completato");
      } else {
        Update.printError(Serial);
      }
      break;

    default:
      break;
  }
}

// ---------------------------------------------------------------------
//  Setup
// ---------------------------------------------------------------------
static void wifiConnectBlocking(uint32_t timeoutMs) {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(OTA_HOSTNAME);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("[WiFi] Connessione a \"%s\"", WIFI_SSID);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("[WiFi] Connesso. IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("[WiFi] Timeout: continuo, ritento in background.");
  }
}

void net_begin() {
  wifiConnectBlocking(15000);

  // --- 1) ArduinoOTA: upload da Arduino IDE (porta di rete) ---
  ArduinoOTA.setHostname(OTA_HOSTNAME);
  if (strlen(OTA_PASSWORD) > 0) ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.onStart([]() {
    Serial.println("[ArduinoOTA] Start");
    if (s_progressCb) s_progressCb(0, "ArduinoOTA...");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    if (s_progressCb && total) s_progressCb((int)(progress * 100 / total), "ArduinoOTA");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("[ArduinoOTA] Fine");
    if (s_progressCb) s_progressCb(100, "Completato");
  });
  ArduinoOTA.onError([](ota_error_t err) {
    Serial.printf("[ArduinoOTA] Errore %u\n", err);
  });
  ArduinoOTA.begin();   // avvia anche mDNS con OTA_HOSTNAME

  // --- 2) Web OTA: pagina /update ---
  MDNS.addService("http", "tcp", 80);
  server.on("/", HTTP_GET, handleRoot);
  server.on("/update", HTTP_GET, handleRoot);   // GET: mostra il form di upload
  server.on("/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);  // POST: riceve il .bin
  server.onNotFound([]() { server.send(404, "text/plain", "Not found"); });
  server.begin();
  Serial.printf("[WebOTA] http://%s.local/update\n", OTA_HOSTNAME);
}

void net_loop() {
  ArduinoOTA.handle();
  server.handleClient();
}
