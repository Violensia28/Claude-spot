// ============================================================
// MOT-CycleControl Firmware v1.0.0
// ESP32 + SSR Zero-Cross + OLED + Rotary Encoder
// 
// Key Features:
// - Cycle-based timing (50 Hz AC → 1 cycle = 20 ms)
// - HELD HIGH signal (not pulse) for SSR Zero-Cross
// - Double Pulse mode (P1 + GAP + P2)
// - Physical UI (OLED + Encoder) + Web UI (SoftAP)
// - OTA updates via AsyncElegantOTA
// ============================================================

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncElegantOTA.h>
#include "Config.h"

// ==================== GLOBAL STATE ====================
// Shared state (synchronized between Physical UI & Web UI)
volatile uint8_t currentLevel_1 = DEFAULT_P1;     // Pulse 1
volatile uint8_t currentLevel_Jeda = DEFAULT_GAP; // Gap/Jeda
volatile uint8_t currentLevel_2 = DEFAULT_P2;     // Pulse 2
volatile uint8_t weldMode = DEFAULT_MODE;         // 0=SINGLE, 1=DOUBLE
volatile bool welding = false;                    // Safety lock flag

// Encoder state
volatile int encoderPos = 0;
volatile int lastEncoderPos = 0;
volatile uint8_t editField = 0; // 0=P1, 1=GAP, 2=P2, 3=MODE

// Button state
volatile unsigned long lastButtonPress = 0;
volatile bool buttonPressed = false;

// Display update timing
unsigned long lastDisplayUpdate = 0;
unsigned long lastEncoderRead = 0;
unsigned long lastWeldTime = 0;

// ==================== HARDWARE OBJECTS ====================
Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);
AsyncWebServer server(WEB_PORT);

// ==================== FUNCTION PROTOTYPES ====================
void setupHardware();
void setupOLED();
void setupEncoder();
void setupTriggerButton();
void setupSSR();
void setupWiFiAP();
void setupWebServer();

void updateDisplay();
void readEncoder();
void checkTriggerButton();
void handleEncoderButton();

void setLevels(uint8_t p1, uint8_t gap, uint8_t p2, uint8_t mode);
void triggerWeld();
void doWeld(uint16_t p1Ms, uint16_t gapMs, uint16_t p2Ms);

String getStateJSON();

// ==================== ENCODER ISR ====================
void IRAM_ATTR encoderISR() {
    static uint8_t lastState = 0;
    uint8_t clk = digitalRead(ENCODER_CLK);
    uint8_t dt = digitalRead(ENCODER_DT);
    uint8_t state = (clk << 1) | dt;
    
    // Simple quadrature decoding
    if (lastState == 0b00 && state == 0b01) encoderPos--;
    if (lastState == 0b00 && state == 0b10) encoderPos++;
    if (lastState == 0b01 && state == 0b11) encoderPos--;
    if (lastState == 0b10 && state == 0b11) encoderPos++;
    
    lastState = state;
}

// ==================== BUTTON ISR ====================
void IRAM_ATTR buttonISR() {
    unsigned long now = millis();
    if (now - lastButtonPress > ENCODER_DEBOUNCE) {
        buttonPressed = true;
        lastButtonPress = now;
    }
}

// ==================== SETUP ====================
void setup() {
    Serial.begin(DEBUG_BAUD);
    delay(500);
    
    Serial.println("\n\n========================================");
    Serial.println("MOT-CycleControl v" APP_VERSION);
    Serial.println("========================================\n");
    
    setupHardware();
    setupOLED();
    setupEncoder();
    setupTriggerButton();
    setupSSR();
    setupWiFiAP();
    setupWebServer();
    
    updateDisplay();
    
    Serial.println("\n✅ System Ready!");
    Serial.printf("📡 Connect to: %s / %s\n", AP_SSID, AP_PASSWORD);
    Serial.printf("🌐 Web UI: http://%s\n", WiFi.softAPIP().toString().c_str());
    Serial.printf("🔧 OTA Update: http://%s%s\n\n", WiFi.softAPIP().toString().c_str(), WEB_UPDATE_PATH);
}

// ==================== MAIN LOOP ====================
void loop() {
    unsigned long now = millis();
    
    // Read encoder at regular intervals
    if (now - lastEncoderRead >= ENCODER_READ_MS) {
        readEncoder();
        lastEncoderRead = now;
    }
    
    // Check encoder button press
    if (buttonPressed) {
        handleEncoderButton();
        buttonPressed = false;
    }
    
    // Check external trigger button
    checkTriggerButton();
    
    // Update display at regular intervals
    if (now - lastDisplayUpdate >= DISPLAY_UPDATE_MS) {
        updateDisplay();
        lastDisplayUpdate = now;
    }
}

// ==================== HARDWARE SETUP ====================
void setupHardware() {
    Serial.println("🔧 Initializing hardware...");
    
    // I2C for OLED
    Wire.begin(OLED_SDA, OLED_SCL);
    Wire.setClock(400000); // Fast I2C (400 kHz)
}

void setupOLED() {
    Serial.print("📺 Initializing OLED... ");
    
    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
        Serial.println("❌ FAILED!");
        Serial.println("⚠️  Check OLED wiring (SDA=21, SCL=22)");
        while (1) delay(100); // Halt on critical failure
    }
    
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("MOT-CycleControl");
    display.println(APP_VERSION);
    display.println("\nInitializing...");
    display.display();
    
    Serial.println("✅ OK");
}

void setupEncoder() {
    Serial.print("🎛️  Initializing encoder... ");
    
    pinMode(ENCODER_CLK, INPUT_PULLUP);
    pinMode(ENCODER_DT, INPUT_PULLUP);
    pinMode(ENCODER_SW, INPUT_PULLUP);
    
    // Attach interrupts
    attachInterrupt(digitalPinToInterrupt(ENCODER_CLK), encoderISR, CHANGE);
    attachInterrupt(digitalPinToInterrupt(ENCODER_DT), encoderISR, CHANGE);
    attachInterrupt(digitalPinToInterrupt(ENCODER_SW), buttonISR, FALLING);
    
    Serial.println("✅ OK");
}

void setupTriggerButton() {
    Serial.print("🔘 Initializing trigger button... ");
    
    pinMode(TRIGGER_BTN_PIN, INPUT_PULLUP);
    
    Serial.println("✅ OK");
}

void setupSSR() {
    Serial.print("⚡ Initializing SSR control... ");
    
    pinMode(SSR_PIN, OUTPUT);
    digitalWrite(SSR_PIN, LOW); // Ensure OFF state
    
    Serial.println("✅ OK");
    Serial.println("⚠️  SSR driven via 5V rail (verify current <12mA)");
}

void setupWiFiAP() {
    Serial.print("📡 Starting Wi-Fi AP... ");
    
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD, AP_CHANNEL, AP_HIDDEN, AP_MAX_CONN);
    
    delay(100); // Wait for AP to stabilize
    
    IPAddress ip = WiFi.softAPIP();
    Serial.printf("✅ OK\n");
    Serial.printf("   SSID: %s\n", AP_SSID);
    Serial.printf("   PASS: %s\n", AP_PASSWORD);
    Serial.printf("   IP:   %s\n", ip.toString().c_str());
}

void setupWebServer() {
    Serial.print("🌐 Starting web server... ");
    
    // Serve enhanced main page with material presets
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
        html += "<title>MOT-CycleControl</title>";
        html += "<style>";
        html += "*{margin:0;padding:0;box-sizing:border-box}";
        html += "body{font-family:'Segoe UI',Tahoma,Geneva,Verdana,sans-serif;background:linear-gradient(135deg,#0f0c29,#302b63,#24243e);color:#fff;min-height:100vh;padding:20px}";
        html += ".container{max-width:700px;margin:0 auto}";
        html += "header{text-align:center;margin-bottom:30px;padding:20px;background:rgba(255,255,255,0.05);border-radius:15px;backdrop-filter:blur(10px)}";
        html += "h1{font-size:2.5em;color:#ff6b35;text-shadow:2px 2px 10px rgba(255,107,53,0.5);margin-bottom:10px}";
        html += ".version{font-size:0.9em;color:#4ecdc4;font-weight:300}";
        html += ".status-bar{display:flex;justify-content:space-between;align-items:center;padding:15px;background:rgba(78,205,196,0.1);border-left:4px solid #4ecdc4;border-radius:8px;margin-bottom:20px}";
        html += ".status-text{font-weight:bold;color:#4ecdc4}";
        html += ".card{background:rgba(255,255,255,0.08);border-radius:15px;padding:25px;margin-bottom:20px;box-shadow:0 8px 32px rgba(0,0,0,0.3);backdrop-filter:blur(10px);border:1px solid rgba(255,255,255,0.1)}";
        html += "h2{color:#4ecdc4;font-size:1.5em;margin-bottom:20px;display:flex;align-items:center;gap:10px}";
        html += ".preset-section{margin-bottom:25px}";
        html += "label{display:block;margin-bottom:8px;font-weight:600;color:#e0e0e0;font-size:0.95em}";
        html += "select{width:100%;padding:12px;background:rgba(255,255,255,0.1);border:2px solid rgba(78,205,196,0.3);border-radius:8px;color:#fff;font-size:1em;cursor:pointer;transition:all 0.3s}";
        html += "select:hover{border-color:#4ecdc4;background:rgba(255,255,255,0.15)}";
        html += "select option{background:#1a1a2e;color:#fff}";
        html += ".param-group{margin-bottom:20px}";
        html += ".param-header{display:flex;justify-content:space-between;align-items:center;margin-bottom:8px}";
        html += ".param-label{font-weight:600;color:#e0e0e0}";
        html += ".param-value{font-size:1.3em;color:#ff6b35;font-weight:bold;min-width:100px;text-align:right}";
        html += ".param-ms{font-size:0.85em;color:#4ecdc4;margin-left:5px}";
        html += "input[type=range]{width:100%;height:8px;border-radius:5px;background:rgba(255,255,255,0.1);outline:none;-webkit-appearance:none;appearance:none}";
        html += "input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;appearance:none;width:24px;height:24px;border-radius:50%;background:#ff6b35;cursor:pointer;box-shadow:0 0 10px rgba(255,107,53,0.5);transition:0.3s}";
        html += "input[type=range]::-webkit-slider-thumb:hover{background:#ff8c61;transform:scale(1.1)}";
        html += "input[type=range]::-moz-range-thumb{width:24px;height:24px;border-radius:50%;background:#ff6b35;cursor:pointer;border:none;box-shadow:0 0 10px rgba(255,107,53,0.5)}";
        html += ".mode-toggle{width:100%;padding:15px;font-size:1.1em;font-weight:bold;border:none;border-radius:10px;cursor:pointer;transition:all 0.3s;background:linear-gradient(135deg,#4ecdc4,#44a5a0);color:#000;box-shadow:0 4px 15px rgba(78,205,196,0.3)}";
        html += ".mode-toggle:hover{transform:translateY(-2px);box-shadow:0 6px 20px rgba(78,205,196,0.4)}";
        html += ".spot-btn{width:100%;padding:20px;font-size:1.4em;font-weight:bold;border:none;border-radius:12px;cursor:pointer;transition:all 0.3s;background:linear-gradient(135deg,#ff6b35,#f7931e);color:#fff;box-shadow:0 8px 25px rgba(255,107,53,0.4);text-transform:uppercase;letter-spacing:1px}";
        html += ".spot-btn:hover:not(:disabled){transform:translateY(-3px);box-shadow:0 12px 35px rgba(255,107,53,0.6)}";
        html += ".spot-btn:active:not(:disabled){transform:translateY(-1px)}";
        html += ".spot-btn:disabled{background:#555;cursor:not-allowed;opacity:0.5;box-shadow:none}";
        html += ".footer{text-align:center;margin-top:30px;padding:20px;color:#888;font-size:0.9em;background:rgba(255,255,255,0.05);border-radius:10px}";
        html += ".footer a{color:#4ecdc4;text-decoration:none;font-weight:600;transition:0.3s}";
        html += ".footer a:hover{color:#6ee0d7;text-decoration:underline}";
        html += ".warning{background:rgba(255,107,53,0.1);border-left:4px solid #ff6b35;padding:12px;border-radius:6px;margin-top:15px;font-size:0.9em;color:#ffb380}";
        html += "@media(max-width:768px){h1{font-size:2em}.param-value{font-size:1.1em}}";
        html += "</style></head><body>";
        html += "<div class='container'>";
        html += "<header><h1>⚡ MOT-CycleControl</h1><div class='version'>Firmware v" + String(APP_VERSION) + " | Cycle-Based Zero-Cross</div></header>";
        html += "<div class='status-bar'><span>🌐 Status:</span><span class='status-text' id='status'>Ready</span></div>";
        
        // Material Preset Section
        html += "<div class='card'><h2>📋 Material Presets</h2>";
        html += "<div class='preset-section'>";
        html += "<label for='material'>Material Type:</label>";
        html += "<select id='material' onchange='loadPreset()'>";
        html += "<option value=''>-- Select Material --</option>";
        html += "<option value='ni_01'>Pure Nickel 0.1mm</option>";
        html += "<option value='ni_015'>Pure Nickel 0.15mm</option>";
        html += "<option value='ni_02'>Pure Nickel 0.2mm</option>";
        html += "<option value='nip_01'>Nickel-Plated 0.1mm</option>";
        html += "<option value='nip_015'>Nickel-Plated 0.15mm</option>";
        html += "<option value='cu_01'>Copper 0.1mm</option>";
        html += "<option value='cu_015'>Copper 0.15mm</option>";
        html += "<option value='custom'>Custom Settings</option>";
        html += "</select>";
        html += "<div class='warning'>⚠️ Presets are conservative starting points. Test and adjust gradually!</div>";
        html += "</div></div>";
        
        // Weld Parameters Section
        html += "<div class='card'><h2>🎛️ Weld Parameters</h2>";
        html += "<div class='param-group'><div class='param-header'><span class='param-label'>Pulse 1 (P1):</span>";
        html += "<span><span class='param-value' id='p1Val'>5</span><span class='param-ms'>cycles (<span id='p1Ms'>100</span>ms)</span></span></div>";
        html += "<input type='range' id='p1' min='1' max='99' value='5' oninput='updateParam(\"p1\")'></div>";
        html += "<div class='param-group'><div class='param-header'><span class='param-label'>Gap:</span>";
        html += "<span><span class='param-value' id='gapVal'>3</span><span class='param-ms'>cycles (<span id='gapMs'>60</span>ms)</span></span></div>";
        html += "<input type='range' id='gap' min='1' max='99' value='3' oninput='updateParam(\"gap\")'></div>";
        html += "<div class='param-group'><div class='param-header'><span class='param-label'>Pulse 2 (P2):</span>";
        html += "<span><span class='param-value' id='p2Val'>7</span><span class='param-ms'>cycles (<span id='p2Ms'>140</span>ms)</span></span></div>";
        html += "<input type='range' id='p2' min='1' max='99' value='7' oninput='updateParam(\"p2\")'></div>";
        html += "<button class='mode-toggle' onclick='toggleMode()'>Mode: <span id='modeText'>SINGLE</span></button></div>";
        
        // Spot Weld Button
        html += "<div class='card'><button class='spot-btn' id='spotBtn' onclick='doSpot()'>🔥 SPOT WELD</button></div>";
        
        // Footer
        html += "<div class='footer'>";
        html += "⏱️ 1 cycle = 20ms @ 50Hz | ";
        html += "<a href='" + String(WEB_UPDATE_PATH) + "'>🔧 OTA Update</a> | ";
        html += "<a href='https://github.com' target='_blank'>📖 Documentation</a>";
        html += "</div></div>";
        
        // JavaScript
        html += "<script>";
        html += "let mode=0;";
        html += "const presets={";
        html += "ni_01:{p1:2,gap:2,p2:3,mode:1},";
        html += "ni_015:{p1:3,gap:3,p2:5,mode:1},";
        html += "ni_02:{p1:4,gap:3,p2:6,mode:1},";
        html += "nip_01:{p1:3,gap:2,p2:4,mode:1},";
        html += "nip_015:{p1:4,gap:3,p2:6,mode:1},";
        html += "cu_01:{p1:5,gap:3,p2:8,mode:1},";
        html += "cu_015:{p1:7,gap:4,p2:10,mode:1}";
        html += "};";
        html += "function loadPreset(){const sel=document.getElementById('material').value;if(!sel||sel==='custom')return;const p=presets[sel];if(p){";
        html += "document.getElementById('p1').value=p.p1;document.getElementById('gap').value=p.gap;document.getElementById('p2').value=p.p2;";
        html += "mode=p.mode;updateParam('p1');updateParam('gap');updateParam('p2');document.getElementById('modeText').textContent=mode?'DOUBLE':'SINGLE';sendLevels()}}";
        html += "function updateParam(id){const val=document.getElementById(id).value;document.getElementById(id+'Val').textContent=val;";
        html += "document.getElementById(id+'Ms').textContent=val*20;sendLevels()}";
        html += "function sendLevels(){const p1=parseInt(document.getElementById('p1').value);const gap=parseInt(document.getElementById('gap').value);";
        html += "const p2=parseInt(document.getElementById('p2').value);fetch('/api/level',{method:'POST',headers:{'Content-Type':'application/json'},";
        html += "body:JSON.stringify({p1:p1,gap:gap,p2:p2,mode:mode})}).catch(e=>console.error(e))}";
        html += "function toggleMode(){mode=mode?0:1;document.getElementById('modeText').textContent=mode?'DOUBLE':'SINGLE';sendLevels()}";
        html += "function doSpot(){const btn=document.getElementById('spotBtn');btn.disabled=true;document.getElementById('status').textContent='WELDING...';";
        html += "fetch('/api/spot',{method:'POST'}).then(r=>{if(r.ok){setTimeout(()=>{btn.disabled=false;document.getElementById('status').textContent='Ready'},2000)}";
        html += "else{btn.disabled=false;document.getElementById('status').textContent='Error: '+r.status}}).catch(e=>{btn.disabled=false;document.getElementById('status').textContent='Error';console.error(e)})}";
        html += "function syncState(){fetch('/api/state').then(r=>r.json()).then(d=>{document.getElementById('p1').value=d.p1;updateParam('p1');";
        html += "document.getElementById('gap').value=d.gap;updateParam('gap');document.getElementById('p2').value=d.p2;updateParam('p2');";
        html += "mode=d.mode;document.getElementById('modeText').textContent=mode?'DOUBLE':'SINGLE';";
        html += "if(!d.welding&&document.getElementById('status').textContent==='WELDING...'){document.getElementById('status').textContent='Ready'}}).catch(e=>console.error(e))}";
        html += "setInterval(syncState,1000);window.onload=()=>{syncState()};";
        html += "</script></body></html>";
        request->send(200, "text/html", html);
    });
    
    // API: Get current state
    server.on("/api/state", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", getStateJSON());
    });
    
    // API: Set levels
    server.on("/api/level", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            if (welding) {
                request->send(503, "text/plain", "Weld in progress");
                return;
            }
            
            // Parse JSON manually (simple approach)
            String body = String((char*)data).substring(0, len);
            
            uint8_t p1 = currentLevel_1;
            uint8_t gap = currentLevel_Jeda;
            uint8_t p2 = currentLevel_2;
            uint8_t mode = weldMode;
            
            // Extract values (basic parsing)
            int idx;
            if ((idx = body.indexOf("\"p1\":")) >= 0) {
                p1 = body.substring(idx + 5).toInt();
            }
            if ((idx = body.indexOf("\"gap\":")) >= 0) {
                gap = body.substring(idx + 6).toInt();
            }
            if ((idx = body.indexOf("\"p2\":")) >= 0) {
                p2 = body.substring(idx + 5).toInt();
            }
            if ((idx = body.indexOf("\"mode\":")) >= 0) {
                mode = body.substring(idx + 7).toInt();
            }
            
            setLevels(p1, gap, p2, mode);
            request->send(200, "application/json", getStateJSON());
        });
    
    // API: Trigger spot weld
    server.on("/api/spot", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (welding) {
            request->send(503, "text/plain", "Weld in progress");
            return;
        }
        
        request->send(200, "text/plain", "OK");
        triggerWeld();
    });
    
    // Setup OTA
    #ifdef ENABLE_OTA
    AsyncElegantOTA.begin(&server);
    Serial.println("🔧 OTA enabled at " WEB_UPDATE_PATH);
    #endif
    
    server.begin();
    Serial.println("✅ OK");
}

// ==================== UI FUNCTIONS ====================
void updateDisplay() {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    
    // Header
    display.println("MOT-CycleControl");
    display.drawLine(0, 10, OLED_WIDTH, 10, SSD1306_WHITE);
    
    // Mode
    display.setCursor(0, 14);
    display.print("Mode: ");
    display.println(weldMode == WELD_MODE_DOUBLE ? "DOUBLE" : "SINGLE");
    
    // Parameters (highlight current edit field)
    display.setCursor(0, 26);
    if (editField == 0) display.print(">");
    display.printf(" P1:  %2d (%dms)\n", currentLevel_1, LEVEL_TO_MS(currentLevel_1));
    
    display.setCursor(0, 36);
    if (editField == 1) display.print(">");
    display.printf(" GAP: %2d (%dms)\n", currentLevel_Jeda, LEVEL_TO_MS(currentLevel_Jeda));
    
    display.setCursor(0, 46);
    if (editField == 2) display.print(">");
    display.printf(" P2:  %2d (%dms)\n", currentLevel_2, LEVEL_TO_MS(currentLevel_2));
    
    // Status
    display.setCursor(0, 56);
    if (welding) {
        display.print(">>> WELDING! <<<");
    } else {
        display.print("Ready");
    }
    
    display.display();
}

void readEncoder() {
    if (welding) return; // Ignore encoder during weld
    
    int delta = encoderPos - lastEncoderPos;
    if (delta == 0) return;
    
    lastEncoderPos = encoderPos;
    
    // Update current field
    switch (editField) {
        case 0: // P1
            currentLevel_1 = CONSTRAIN_LEVEL(currentLevel_1 + delta);
            break;
        case 1: // GAP
            currentLevel_Jeda = CONSTRAIN_LEVEL(currentLevel_Jeda + delta);
            break;
        case 2: // P2
            currentLevel_2 = CONSTRAIN_LEVEL(currentLevel_2 + delta);
            break;
        case 3: // MODE
            weldMode = (delta > 0) ? WELD_MODE_DOUBLE : WELD_MODE_SINGLE;
            break;
    }
    
    Serial.printf("🎛️  P1=%d GAP=%d P2=%d MODE=%s\n", 
        currentLevel_1, currentLevel_Jeda, currentLevel_2,
        weldMode ? "DOUBLE" : "SINGLE");
}

void handleEncoderButton() {
    if (welding) return; // Ignore button during weld
    
    // Cycle through edit fields: P1 -> GAP -> P2 -> MODE -> P1
    editField = (editField + 1) % 4;
    
    Serial.printf("📝 Edit field: %s\n", 
        editField == 0 ? "P1" : 
        editField == 1 ? "GAP" : 
        editField == 2 ? "P2" : "MODE");
    
    updateDisplay();
}

void checkTriggerButton() {
    static unsigned long lastCheck = 0;
    unsigned long now = millis();
    
    if (now - lastCheck < 50) return; // Debounce
    lastCheck = now;
    
    if (digitalRead(TRIGGER_BTN_PIN) == TRIGGER_ACTIVE) {
        triggerWeld();
        delay(200); // Prevent multiple triggers
    }
}

// ==================== WELD LOGIC ====================
void setLevels(uint8_t p1, uint8_t gap, uint8_t p2, uint8_t mode) {
    if (welding) return; // Prevent changes during weld
    
    currentLevel_1 = CONSTRAIN_LEVEL(p1);
    currentLevel_Jeda = CONSTRAIN_LEVEL(gap);
    currentLevel_2 = CONSTRAIN_LEVEL(p2);
    weldMode = (mode > 0) ? WELD_MODE_DOUBLE : WELD_MODE_SINGLE;
    
    updateDisplay();
}

void triggerWeld() {
    // Re-entrancy protection
    if (welding) {
        Serial.println("⚠️  WELD IN PROGRESS - IGNORED");
        return;
    }
    
    // Cooldown check
    unsigned long now = millis();
    if (now - lastWeldTime < MIN_COOLDOWN_MS) {
        Serial.println("⚠️  COOLDOWN ACTIVE - WAIT");
        return;
    }
    
    // Execute weld
    uint16_t p1Ms = LEVEL_TO_MS(currentLevel_1);
    uint16_t gapMs = LEVEL_TO_MS(currentLevel_Jeda);
    uint16_t p2Ms = LEVEL_TO_MS(currentLevel_2);
    
    Serial.println("\n🔥 === WELD STARTED ===");
    Serial.printf("   Mode: %s\n", weldMode ? "DOUBLE" : "SINGLE");
    Serial.printf("   P1:   %dms (%d cycles)\n", p1Ms, currentLevel_1);
    if (weldMode == WELD_MODE_DOUBLE) {
        Serial.printf("   GAP:  %dms (%d cycles)\n", gapMs, currentLevel_Jeda);
        Serial.printf("   P2:   %dms (%d cycles)\n", p2Ms, currentLevel_2);
    }
    
    updateDisplay(); // Show "WELDING!" status
    
    doWeld(p1Ms, gapMs, p2Ms);
    
    lastWeldTime = millis();
    
    Serial.println("✅ === WELD COMPLETE ===\n");
    
    updateDisplay(); // Restore normal display
}

void doWeld(uint16_t p1Ms, uint16_t gapMs, uint16_t p2Ms) {
    welding = true; // Lock flag
    
    // PULSE 1: HELD HIGH
    digitalWrite(SSR_PIN, HIGH);
    delay(p1Ms);
    digitalWrite(SSR_PIN, LOW);
    
    // DOUBLE PULSE MODE
    if (weldMode == WELD_MODE_DOUBLE) {
        // GAP
        delay(gapMs);
        
        // PULSE 2: HELD HIGH
        digitalWrite(SSR_PIN, HIGH);
        delay(p2Ms);
        digitalWrite(SSR_PIN, LOW);
    }
    
    welding = false; // Unlock flag
}

// ==================== UTILITY FUNCTIONS ====================
String getStateJSON() {
    char json[256];
    snprintf(json, sizeof(json),
        "{\"version\":\"%s\",\"mode\":%d,\"p1\":%d,\"gap\":%d,\"p2\":%d,\"welding\":%s,\"uptime_ms\":%lu}",
        APP_VERSION,
        weldMode,
        currentLevel_1,
        currentLevel_Jeda,
        currentLevel_2,
        welding ? "true" : "false",
        millis()
    );
    return String(json);
}