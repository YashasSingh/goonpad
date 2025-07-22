#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <USB.h>
#include <USBHIDKeyboard.h>

// GPIO pin definitions (placeholder - adjust based on your wiring)
#define ROW1_PIN 0    // Row 1 of key matrix
#define ROW2_PIN 1    // Row 2 of key matrix
#define COL1_PIN 2    // Column 1 of key matrix
#define COL2_PIN 3    // Column 2 of key matrix
#define COL3_PIN 4    // Column 3 of key matrix
#define COL4_PIN 5    // Column 4 of key matrix
#define ENCODER_A 6   // Rotary encoder pin A
#define ENCODER_B 7   // Rotary encoder pin B
#define ENCODER_BTN 8 // Rotary encoder button

// Key matrix configuration
const int ROWS = 2;
const int COLS = 4;
const int TOTAL_KEYS = 8;

// Pin arrays for matrix scanning
int rowPins[ROWS] = {ROW1_PIN, ROW2_PIN};
int colPins[COLS] = {COL1_PIN, COL2_PIN, COL3_PIN, COL4_PIN};

// Key state tracking
bool keyStates[TOTAL_KEYS] = {false};
bool lastKeyStates[TOTAL_KEYS] = {false};
unsigned long lastDebounceTime[TOTAL_KEYS] = {0};
const unsigned long debounceDelay = 50;

// Rotary encoder variables
int lastEncoderA = 0;
int lastEncoderB = 0;
bool lastEncoderBtn = false;
unsigned long lastEncoderTime = 0;

// USB HID Keyboard
USBHIDKeyboard keyboard;

// Web server for configuration
WebServer server(80);
Preferences preferences;

// Macro storage structure
struct MacroKey {
  String name;
  String action;
  uint8_t keyCode;
  uint8_t modifier;
  bool isString;
};

MacroKey macroKeys[TOTAL_KEYS];
MacroKey encoderCW, encoderCCW, encoderBtn;

void setup() {
  Serial.begin(115200);
  
  // Initialize GPIO pins
  initializeGPIO();
  
  // Initialize USB HID
  USB.begin();
  keyboard.begin();
  
  // Initialize preferences
  preferences.begin("macropad", false);
  
  // Load saved macros
  loadMacros();
  
  // Initialize WiFi AP for configuration
  initializeWiFi();
  
  // Setup web server routes
  setupWebServer();
  
  Serial.println("USB Hub Macro Pad initialized!");
  Serial.println("Connect to WiFi: MacroPad_Config");
  Serial.println("Password: macropad123");
  Serial.println("Configuration page: http://192.168.4.1");
}

void loop() {
  // Scan key matrix
  scanKeyMatrix();
  
  // Check rotary encoder
  checkRotaryEncoder();
  
  // Handle web server
  server.handleClient();
  
  delay(10);
}

void initializeGPIO() {
  // Initialize row pins as outputs
  for (int i = 0; i < ROWS; i++) {
    pinMode(rowPins[i], OUTPUT);
    digitalWrite(rowPins[i], HIGH);
  }
  
  // Initialize column pins as inputs with pullup
  for (int i = 0; i < COLS; i++) {
    pinMode(colPins[i], INPUT_PULLUP);
  }
  
  // Initialize encoder pins
  pinMode(ENCODER_A, INPUT_PULLUP);
  pinMode(ENCODER_B, INPUT_PULLUP);
  pinMode(ENCODER_BTN, INPUT_PULLUP);
  
  // Read initial encoder state
  lastEncoderA = digitalRead(ENCODER_A);
  lastEncoderB = digitalRead(ENCODER_B);
  lastEncoderBtn = digitalRead(ENCODER_BTN);
}

void scanKeyMatrix() {
  for (int row = 0; row < ROWS; row++) {
    // Set current row low
    digitalWrite(rowPins[row], LOW);
    
    for (int col = 0; col < COLS; col++) {
      int keyIndex = row * COLS + col;
      bool currentState = !digitalRead(colPins[col]); // Inverted due to pullup
      
      // Debouncing
      if (currentState != lastKeyStates[keyIndex]) {
        lastDebounceTime[keyIndex] = millis();
      }
      
      if ((millis() - lastDebounceTime[keyIndex]) > debounceDelay) {
        if (currentState != keyStates[keyIndex]) {
          keyStates[keyIndex] = currentState;
          
          if (currentState) {
            // Key pressed
            executeMacro(keyIndex);
            Serial.printf("Key %d pressed\n", keyIndex);
          }
        }
      }
      
      lastKeyStates[keyIndex] = currentState;
    }
    
    // Set row back to high
    digitalWrite(rowPins[row], HIGH);
  }
}

void checkRotaryEncoder() {
  int currentA = digitalRead(ENCODER_A);
  int currentB = digitalRead(ENCODER_B);
  bool currentBtn = digitalRead(ENCODER_BTN);
  
  // Check for rotation
  if (currentA != lastEncoderA) {
    if (millis() - lastEncoderTime > 5) { // Debounce
      if (currentA == currentB) {
        // Clockwise
        executeMacro(encoderCW);
        Serial.println("Encoder CW");
      } else {
        // Counter-clockwise
        executeMacro(encoderCCW);
        Serial.println("Encoder CCW");
      }
      lastEncoderTime = millis();
    }
  }
  
  // Check for button press
  if (currentBtn != lastEncoderBtn && !currentBtn) {
    executeMacro(encoderBtn);
    Serial.println("Encoder button pressed");
    delay(200); // Simple debounce
  }
  
  lastEncoderA = currentA;
  lastEncoderB = currentB;
  lastEncoderBtn = currentBtn;
}

void executeMacro(int keyIndex) {
  if (keyIndex >= 0 && keyIndex < TOTAL_KEYS) {
    executeMacro(macroKeys[keyIndex]);
  }
}

void executeMacro(MacroKey macro) {
  if (macro.action.length() == 0) return;
  
  if (macro.isString) {
    // Type string
    keyboard.print(macro.action);
  } else {
    // Send key combination
    if (macro.modifier != 0) {
      keyboard.press(macro.modifier);
    }
    if (macro.keyCode != 0) {
      keyboard.press(macro.keyCode);
    }
    delay(50);
    keyboard.releaseAll();
  }
}

void initializeWiFi() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP("MacroPad_Config", "macropad123");
  
  Serial.println();
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());
}

void setupWebServer() {
  // Main configuration page
  server.on("/", handleRoot);
  
  // API endpoints
  server.on("/api/macros", HTTP_GET, handleGetMacros);
  server.on("/api/macros", HTTP_POST, handleSetMacros);
  server.on("/api/save", HTTP_POST, handleSaveMacros);
  
  server.begin();
}

void handleRoot() {
  String html = R"(
<!DOCTYPE html>
<html>
<head>
    <title>Macro Pad Configuration</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; }
        .key-config { border: 1px solid #ccc; padding: 10px; margin: 10px 0; }
        input, select { width: 200px; margin: 5px; }
        button { padding: 10px 20px; background: #007cba; color: white; border: none; cursor: pointer; }
        .encoder-section { background: #f5f5f5; padding: 15px; margin: 20px 0; }
    </style>
</head>
<body>
    <h1>USB Hub Macro Pad Configuration</h1>
    
    <h2>Macro Keys (1-8)</h2>
    <div id="keyConfigs"></div>
    
    <div class="encoder-section">
        <h2>Rotary Encoder</h2>
        <div class="key-config">
            <h3>Clockwise Rotation</h3>
            <input type="text" id="encoderCW_name" placeholder="Action Name">
            <input type="text" id="encoderCW_action" placeholder="Text to type or key combo">
        </div>
        <div class="key-config">
            <h3>Counter-Clockwise Rotation</h3>
            <input type="text" id="encoderCCW_name" placeholder="Action Name">
            <input type="text" id="encoderCCW_action" placeholder="Text to type or key combo">
        </div>
        <div class="key-config">
            <h3>Button Press</h3>
            <input type="text" id="encoderBtn_name" placeholder="Action Name">
            <input type="text" id="encoderBtn_action" placeholder="Text to type or key combo">
        </div>
    </div>
    
    <button onclick="saveMacros()">Save Configuration</button>
    <button onclick="loadMacros()">Load Current Config</button>
    
    <script>
        // Generate key configuration inputs
        for (let i = 0; i < 8; i++) {
            document.getElementById('keyConfigs').innerHTML += `
                <div class="key-config">
                    <h3>Key ${i + 1}</h3>
                    <input type="text" id="key${i}_name" placeholder="Action Name">
                    <input type="text" id="key${i}_action" placeholder="Text to type or key combo">
                    <select id="key${i}_type">
                        <option value="string">Type Text</option>
                        <option value="key">Key Combination</option>
                    </select>
                </div>
            `;
        }
        
        function saveMacros() {
            const config = {
                keys: [],
                encoder: {}
            };
            
            // Collect key configurations
            for (let i = 0; i < 8; i++) {
                config.keys.push({
                    name: document.getElementById(`key${i}_name`).value,
                    action: document.getElementById(`key${i}_action`).value,
                    type: document.getElementById(`key${i}_type`).value
                });
            }
            
            // Collect encoder configurations
            config.encoder = {
                cw: {
                    name: document.getElementById('encoderCW_name').value,
                    action: document.getElementById('encoderCW_action').value
                },
                ccw: {
                    name: document.getElementById('encoderCCW_name').value,
                    action: document.getElementById('encoderCCW_action').value
                },
                btn: {
                    name: document.getElementById('encoderBtn_name').value,
                    action: document.getElementById('encoderBtn_action').value
                }
            };
            
            fetch('/api/save', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(config)
            }).then(response => {
                if (response.ok) {
                    alert('Configuration saved successfully!');
                } else {
                    alert('Error saving configuration');
                }
            });
        }
        
        function loadMacros() {
            fetch('/api/macros')
                .then(response => response.json())
                .then(config => {
                    // Load key configurations
                    for (let i = 0; i < 8; i++) {
                        if (config.keys[i]) {
                            document.getElementById(`key${i}_name`).value = config.keys[i].name || '';
                            document.getElementById(`key${i}_action`).value = config.keys[i].action || '';
                            document.getElementById(`key${i}_type`).value = config.keys[i].type || 'string';
                        }
                    }
                    
                    // Load encoder configurations
                    if (config.encoder) {
                        document.getElementById('encoderCW_name').value = config.encoder.cw?.name || '';
                        document.getElementById('encoderCW_action').value = config.encoder.cw?.action || '';
                        document.getElementById('encoderCCW_name').value = config.encoder.ccw?.name || '';
                        document.getElementById('encoderCCW_action').value = config.encoder.ccw?.action || '';
                        document.getElementById('encoderBtn_name').value = config.encoder.btn?.name || '';
                        document.getElementById('encoderBtn_action').value = config.encoder.btn?.action || '';
                    }
                });
        }
        
        // Load configuration on page load
        loadMacros();
    </script>
</body>
</html>
  )";
  
  server.send(200, "text/html", html);
}

void handleGetMacros() {
  String json = "{\"keys\":[";
  
  for (int i = 0; i < TOTAL_KEYS; i++) {
    if (i > 0) json += ",";
    json += "{";
    json += "\"name\":\"" + macroKeys[i].name + "\",";
    json += "\"action\":\"" + macroKeys[i].action + "\",";
    json += "\"type\":\"" + (macroKeys[i].isString ? "string" : "key") + "\"";
    json += "}";
  }
  
  json += "],\"encoder\":{";
  json += "\"cw\":{\"name\":\"" + encoderCW.name + "\",\"action\":\"" + encoderCW.action + "\"},";
  json += "\"ccw\":{\"name\":\"" + encoderCCW.name + "\",\"action\":\"" + encoderCCW.action + "\"},";
  json += "\"btn\":{\"name\":\"" + encoderBtn.name + "\",\"action\":\"" + encoderBtn.action + "\"}";
  json += "}}";
  
  server.send(200, "application/json", json);
}

void handleSetMacros() {
  // Implementation for setting macros via POST request
  server.send(200, "text/plain", "Macros updated");
}

void handleSaveMacros() {
  String body = server.arg("plain");
  
  // Parse JSON and save to preferences
  // This is a simplified version - you'd want proper JSON parsing
  
  preferences.putString("config", body);
  server.send(200, "text/plain", "Configuration saved");
}

void loadMacros() {
  // Load default macros or from preferences
  String savedConfig = preferences.getString("config", "");
  
  if (savedConfig.length() == 0) {
    // Set default macros
    macroKeys[0] = {"Copy", "Ctrl+C", KEY_LEFT_CTRL, 'c', false};
    macroKeys[1] = {"Paste", "Ctrl+V", KEY_LEFT_CTRL, 'v', false};
    macroKeys[2] = {"Undo", "Ctrl+Z", KEY_LEFT_CTRL, 'z', false};
    macroKeys[3] = {"Save", "Ctrl+S", KEY_LEFT_CTRL, 's', false};
    macroKeys[4] = {"New Tab", "Ctrl+T", KEY_LEFT_CTRL, 't', false};
    macroKeys[5] = {"Close Tab", "Ctrl+W", KEY_LEFT_CTRL, 'w', false};
    macroKeys[6] = {"Find", "Ctrl+F", KEY_LEFT_CTRL, 'f', false};
    macroKeys[7] = {"Select All", "Ctrl+A", KEY_LEFT_CTRL, 'a', false};
    
    encoderCW = {"Volume Up", "Vol+", KEY_MEDIA_VOLUME_UP, 0, false};
    encoderCCW = {"Volume Down", "Vol-", KEY_MEDIA_VOLUME_DOWN, 0, false};
    encoderBtn = {"Mute", "Mute", KEY_MEDIA_MUTE, 0, false};
  } else {
    // Parse saved configuration
    // Implementation depends on your JSON parsing preference
  }
}