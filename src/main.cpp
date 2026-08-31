/**
 * 🏠 ESP32 Hogar - Agente de Monitoreo de Red
 * 
 * Hardware: ESP32 + Radar RCWL-0516 + DHT22 (temp/humedad)
 * Protector de red con modo agente oculto.
 * 
 * Para el curso de ciberseguridad.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include "network/guardian.h"
#include "presence.h"
#include "heatmap.h"
#include "scanner.h"

// ==================== CONFIGURACIÓN ====================

const char* ssid = "OD";
const char* password = "khce2946";

// Pines
#define PIR_PIN 13         // RCWL-0516 radar de microondas (OUT)
#define DHT_PIN 15         // DHT22 DATA (sensor celeste cuadrado)
#define DHT_TYPE DHT11    // LC-226 usa protocolo DHT compatible

// ==================== VARIABLES ====================

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
DHT dht(DHT_PIN, DHT_TYPE);
NetworkGuardian guardian;
PresenceDetector presence;
HeatmapGenerator heatmap;
NetworkScanner scanner;

// WiFi scan cache
String cachedWifiJson = "{}";

// Datos DHT22 (cache cada 2 segundos)
float currentTemp = 0;
float currentHumidity = 0;
unsigned long lastDhtRead = 0;
#define DHT_READ_INTERVAL 2000

// Forward declarations
void setupWebServer();
void broadcastPresence();
void broadcastWaveform();
void cachedWifiScan();

// Datos del radar RCWL-0516
bool motionDetected = false;
int motionCount = 0;
unsigned long lastMotionTime = 0;

// ==================== SETUP ====================

void setup() {
    Serial.begin(115200);
    Serial.println("\n🏠 ESP32 Hogar - Agente de Red");

    // RCWL-0516 radar
    pinMode(PIR_PIN, INPUT);

    // DHT22 sensor
    dht.begin();
    Serial.println("✅ DHT22 OK (pin " + String(DHT_PIN) + ")");

    // WiFi
    WiFi.begin(ssid, password);
    Serial.print("Conectando a WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\n✅ WiFi conectado!");
    Serial.print("📡 IP: ");
    Serial.println(WiFi.localIP());

    // Protector de red
    guardian.begin();
    Serial.println("✅ Protector de red OK");
    
    // Detector de presencia
    presence.begin();
    Serial.println("✅ Detector de presencia OK");
    
    // Heatmap
    heatmap.begin();
    Serial.println("✅ Heatmap OK");
    
    // Scanner de red
    scanner.begin();
    Serial.println("✅ Network Scanner OK");
    Serial.println("\n🌐 Dashboard: http://" + WiFi.localIP().toString());
    Serial.println("🔍 Presiona el botón en la web para escanear\n");

    // Servidor web
    setupWebServer();
    server.begin();
    Serial.println("✅ Servidor web OK");
    Serial.println("🌐 Abre http://" + WiFi.localIP().toString());

    // Lectura inicial DHT22
    delay(2000);
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    if (isnan(t) || isnan(h)) {
        Serial.println("⚠️ DHT22 NO responde - verifica:");
        Serial.println("   1. Cable VCC → 3.3V o 5V");
        Serial.println("   2. Cable DATA → GPIO4");
        Serial.println("   3. Cable GND → GND");
        Serial.println("   4. Resistencia 10K entre VCC y DATA");
    } else {
        currentTemp = t;
        currentHumidity = h;
        Serial.printf("🌡️ %.1f°C | 💧 %.1f%%\n", currentTemp, currentHumidity);
    }
}

// ==================== LOOP ====================

void loop() {
    // Muestreo ultra rápido: 100ms (10 muestras/segundo)
    static unsigned long lastSample = 0;
    if (millis() - lastSample >= SAMPLE_INTERVAL) {
        bool currentMotion = digitalRead(PIR_PIN) == HIGH;
        
        if (currentMotion && !motionDetected) {
            motionCount++;
            lastMotionTime = millis();
        }
        motionDetected = currentMotion;
        
        // Muestreo PRO: análisis completo
        presence.sample(currentMotion);
        lastSample = millis();
        
        // Broadcast WebSocket si hay clientes
        static bool lastBroadcastState = false;
        if (presence.isPresent() != lastBroadcastState || presence.getIntensity() > 0.1) {
            broadcastPresence();
            lastBroadcastState = presence.isPresent();
        }
    }

    // Escanear red + WiFi cada 30 segundos (UNA sola vez)
    static unsigned long lastScan = 0;
    if (millis() - lastScan > 30000) {
        cachedWifiScan();
        lastScan = millis();
    }

    // Leer DHT22 cada 2 segundos
    if (millis() - lastDhtRead > DHT_READ_INTERVAL) {
        float t = dht.readTemperature();
        float h = dht.readHumidity();
        if (isnan(t) || isnan(h)) {
            Serial.println("⚠️ DHT22: lectura fallida - verifica cables VCC/DATA/GND");
        } else {
            currentTemp = t;
            currentHumidity = h;
            Serial.printf("🌡️ %.1f°C | 💧 %.1f%%\n", currentTemp, currentHumidity);
        }
        lastDhtRead = millis();
    }
    
    // Broadcast waveform cada 200ms via WebSocket
    static unsigned long lastWaveBroadcast = 0;
    if (millis() - lastWaveBroadcast > 200) {
        broadcastWaveform();
        lastWaveBroadcast = millis();
    }

    delay(10);
}



// ==================== WIFI SCAN CACHE ====================

void cachedWifiScan() {
    // UN solo scan, cachea todo
    int n = WiFi.scanNetworks();
    
    // 1. Cache WiFi spectrum
    {
        JsonDocument doc;
        JsonArray networks = doc["networks"].to<JsonArray>();
        int channelCount[14] = {0};
        
        for (int i = 0; i < n; i++) {
            JsonObject net = networks.add<JsonObject>();
            net["ssid"] = WiFi.SSID(i);
            net["rssi"] = WiFi.RSSI(i);
            net["channel"] = WiFi.channel(i);
            net["bssid"] = WiFi.BSSIDstr(i);
            net["secure"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
            int ch = WiFi.channel(i);
            if (ch >= 1 && ch <= 13) channelCount[ch]++;
        }
        JsonArray channels = doc["channels"].to<JsonArray>();
        for (int i = 1; i <= 13; i++) channels.add(channelCount[i]);
        doc["total"] = n;
        doc["ourChannel"] = WiFi.channel();
        doc["ourRSSI"] = WiFi.RSSI();
        doc["ourSSID"] = WiFi.SSID();
        serializeJson(doc, cachedWifiJson);
    }
    WiFi.scanDelete();
    
    // 2. Actualizar heatmap con datos WiFi reales
    heatmap.scanWifi();
    
    // 3. Guardian + Presence (sin WiFi scan)
    guardian.scanNetwork();
    presence.updateWiFi(guardian.getDeviceCount());
    heatmap.simulatePresence(motionDetected);
    
    Serial.printf("🔄 Cache actualizado: %d redes WiFi, %d dispositivos\n", n, guardian.getDeviceCount());
}

// ==================== WEBSOCKET ====================

void broadcastPresence() {
    if (ws.count() == 0) return;
    ws.textAll(presence.getStateJSON());
}

void broadcastWaveform() {
    if (ws.count() == 0) return;
    ws.textAll("W:" + presence.getWaveformJSON());
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                AwsEventType type, void *arg, uint8_t *data, size_t len) {
    switch (type) {
        case WS_EVT_CONNECT:
            Serial.printf("🔗 WebSocket client #%u connected\n", client->id());
            break;
        case WS_EVT_DISCONNECT:
            Serial.printf("🔌 WebSocket client #%u disconnected\n", client->id());
            break;
        case WS_EVT_DATA:
            break;
        case WS_EVT_ERROR:
            break;
    }
}

// ==================== SERVIDOR WEB ====================

void setupWebServer() {
    ws.onEvent(onWsEvent);
    server.addHandler(&ws);
    // Dashboard principal
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        String html = R"rawliteral(
<!DOCTYPE html>
<html lang="es">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>🏠 Hogar ESP32</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
            background: #0a0a0a;
            color: #fff;
            min-height: 100vh;
            padding: 20px;
        }
        .header {
            text-align: center;
            padding: 20px 0;
            border-bottom: 1px solid #333;
            margin-bottom: 20px;
        }
        .header h1 { font-size: 1.8em; }
        .status {
            display: inline-block;
            padding: 5px 15px;
            border-radius: 20px;
            font-size: 0.9em;
            margin-top: 10px;
        }
        .status-ok { background: #16a34a; color: #fff; }
        .status-alert { background: #dc2626; color: #fff; animation: pulse 1.5s infinite; }
        @keyframes pulse { 0%, 100% { opacity: 1; } 50% { opacity: 0.7; } }
        .grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(150px, 1fr));
            gap: 15px;
            margin-bottom: 20px;
        }
        .card {
            background: #1a1a1a;
            border-radius: 12px;
            padding: 20px;
            text-align: center;
            border: 1px solid #333;
        }
        .card .icon { font-size: 2em; margin-bottom: 10px; }
        .card .value { font-size: 1.8em; font-weight: bold; }
        .card .label { color: #888; font-size: 0.9em; margin-top: 5px; }
        .card.alert { border-color: #ef4444; background: #1a0a0a; }
        .section {
            background: #1a1a1a;
            border-radius: 12px;
            padding: 20px;
            margin-bottom: 20px;
            border: 1px solid #333;
        }
        .section h2 { font-size: 1.2em; margin-bottom: 15px; color: #22c55e; }
        .device-list { list-style: none; }
        .device-list li {
            padding: 10px;
            border-bottom: 1px solid #333;
            display: flex;
            justify-content: space-between;
            align-items: center;
        }
        .device-list li:last-child { border-bottom: none; }
        .device-name { font-weight: bold; }
        .device-ip { color: #888; font-size: 0.9em; }
        .device-trust { padding: 3px 10px; border-radius: 10px; font-size: 0.8em; }
        .trust-ok { background: #22c55e33; color: #22c55e; }
        .trust-new { background: #f59e0b33; color: #f59e0b; }
        .trust-suspect { background: #ef444433; color: #ef4444; }
        .alerts { max-height: 200px; overflow-y: auto; }
        .alert-item {
            padding: 10px;
            margin-bottom: 10px;
            background: #1a0a0a;
            border-left: 3px solid #ef4444;
            border-radius: 4px;
        }
        .alert-time { color: #888; font-size: 0.8em; }
        .refresh-btn {
            position: fixed;
            bottom: 20px;
            right: 20px;
            background: #22c55e;
            color: #000;
            border: none;
            border-radius: 50%;
            width: 50px;
            height: 50px;
            font-size: 1.5em;
            cursor: pointer;
            box-shadow: 0 4px 15px rgba(34, 197, 94, 0.3);
        }
        @media (max-width: 768px) {
            body { padding: 15px; }
            .header h1 { font-size: 1.4em; }
            .section { padding: 15px; }
        }
        @media (max-width: 480px) {
            .grid { grid-template-columns: repeat(2, 1fr); gap: 10px; }
            .card { padding: 15px; }
            .card .icon { font-size: 1.5em; }
            .card .value { font-size: 1.4em; }
        }
    </style>
</head>
<body>
    <div class="header">
        <h1>🏠 Hogar ESP32</h1>
        <div id="status" class="status status-ok">● Sistema Activo</div>
    </div>

    <div class="grid">
        <div class="card" id="motionCard">
            <div class="icon">🚶</div>
            <div class="value" id="motion">No</div>
            <div class="label">Movimiento</div>
        </div>
        <div class="card">
            <div class="icon">📊</div>
            <div class="value" id="motionCount">0</div>
            <div class="label">Eventos</div>
        </div>
        <div class="card">
            <div class="icon">📡</div>
            <div class="value" id="devices">0</div>
            <div class="label">Dispositivos en Red</div>
        </div>
        <div class="card">
            <div class="icon">📶</div>
            <div class="value" id="rssi">--</div>
            <div class="label">Signal (dBm)</div>
        </div>
        <div class="card">
            <div class="icon">🌡️</div>
            <div class="value" id="temperature">--°C</div>
            <div class="label">Temperatura</div>
        </div>
        <div class="card">
            <div class="icon">💧</div>
            <div class="value" id="humidity">--%</div>
            <div class="label">Humedad</div>
        </div>
    </div>

    <div class="section">
        <h2>🔒 Protector de Red</h2>
        <div id="networkInfo">Escaneando...</div>
        <ul class="device-list" id="deviceList"></ul>
    </div>

    <div class="section" style="border:2px solid #ef4444;">
        <h2>🔍 Scanner de Red - Vulnerabilidades</h2>
        <div style="display:flex;gap:15px;flex-wrap:wrap;margin-bottom:15px;">
            <div style="flex:1;min-width:100px;text-align:center;padding:10px;background:#0a0a0a;border-radius:8px;">
                <div style="font-size:2em;font-weight:bold;color:#3b82f6;" id="scanDevices">0</div>
                <div style="color:#888;font-size:0.8em;">Dispositivos</div>
            </div>
            <div style="flex:1;min-width:100px;text-align:center;padding:10px;background:#0a0a0a;border-radius:8px;">
                <div style="font-size:2em;font-weight:bold;color:#f59e0b;" id="scanPorts">0</div>
                <div style="color:#888;font-size:0.8em;">Puertos Abiertos</div>
            </div>
            <div style="flex:1;min-width:100px;text-align:center;padding:10px;background:#0a0a0a;border-radius:8px;">
                <div style="font-size:2em;font-weight:bold;color:#ef4444;" id="scanHighRisk">0</div>
                <div style="color:#888;font-size:0.8em;">Alto Riesgo</div>
            </div>
            <div style="flex:1;min-width:100px;text-align:center;padding:10px;background:#0a0a0a;border-radius:8px;">
                <div style="font-size:2em;font-weight:bold;color:#f59e0b;" id="scanMedRisk">0</div>
                <div style="color:#888;font-size:0.8em;">Medio Riesgo</div>
            </div>
        </div>
        <button id="scanBtn" onclick="startScan()" aria-label="Iniciar escaneo de red" style="background:#ef4444;color:#fff;border:none;padding:10px 20px;border-radius:8px;cursor:pointer;font-size:1em;margin-bottom:15px;">🔍 Escanear Red</button>
        <div id="scanStatus" style="color:#888;margin-bottom:10px;"></div>
        <div id="scanResults"></div>
    </div>

    <div class="section" style="border:2px solid #3b82f6;">
        <h2>🗺️ Mapas de Calor (Heatmaps)</h2>
        <div style="display:flex;gap:20px;flex-wrap:wrap;">
            <div style="flex:1;min-width:300px;">
                <h3 style="color:#22c55e;font-size:1em;margin-bottom:10px;">📶 WiFi Signal Strength</h3>
                <canvas id="wifiHeatmap" width="400" height="300" style="width:100%;border-radius:8px;background:#111;"></canvas>
                <div style="display:flex;justify-content:space-between;margin-top:5px;">
                    <span style="color:#22c55e;font-size:0.8em;">🟢 Fuerte (-30dBm)</span>
                    <span style="color:#f59e0b;font-size:0.8em;">🟡 Medio (-70dBm)</span>
                    <span style="color:#ef4444;font-size:0.8em;">🔴 Débil (-90dBm)</span>
                </div>
            </div>
            <div style="flex:1;min-width:300px;">
                <h3 style="color:#3b82f6;font-size:1em;margin-bottom:10px;">📡 Radar Presence Map</h3>
                <canvas id="radarHeatmap" width="400" height="300" style="width:100%;border-radius:8px;background:#111;"></canvas>
                <div style="display:flex;justify-content:space-between;margin-top:5px;">
                    <span style="color:#3b82f6;font-size:0.8em;">🔵 Detectado</span>
                    <span style="color:#f59e0b;font-size:0.8em;">🟡 Actividad media</span>
                    <span style="color:#ef4444;font-size:0.8em;">🔴 Alta actividad</span>
                </div>
            </div>
        </div>
        <div id="heatmapInfo" style="margin-top:10px;color:#888;font-size:0.9em;">Escaneando...</div>
    </div>

    <div class="section" style="border:2px solid #22c55e;">
        <h2>🧑 Detección de Presencia PRO</h2>
        
        <!-- Estado principal -->
        <div style="display:flex;gap:15px;flex-wrap:wrap;margin-bottom:15px;">
            <div style="flex:1;min-width:120px;text-align:center;padding:15px;background:#0a0a0a;border-radius:12px;">
                <div style="font-size:3em;" id="presenceIcon">👤</div>
                <div style="font-size:1.3em;font-weight:bold;color:#22c55e;" id="presenceStatus">Ausente</div>
            </div>
            <div style="flex:1;min-width:120px;text-align:center;padding:15px;background:#0a0a0a;border-radius:12px;">
                <div style="font-size:2em;font-weight:bold;color:#3b82f6;" id="presencePeople">0</div>
                <div style="color:#888;font-size:0.8em;">Personas</div>
            </div>
            <div style="flex:1;min-width:120px;text-align:center;padding:15px;background:#0a0a0a;border-radius:12px;">
                <div style="font-size:2em;font-weight:bold;color:#f59e0b;" id="presenceDuration">0ms</div>
                <div style="color:#888;font-size:0.8em;">Duración evento</div>
            </div>
            <div style="flex:1;min-width:120px;text-align:center;padding:15px;background:#0a0a0a;border-radius:12px;">
                <div style="font-size:2em;font-weight:bold;color:#ef4444;" id="presenceIntensity">0%</div>
                <div style="color:#888;font-size:0.8em;">Intensidad</div>
            </div>
        </div>
        
        <!-- Barra de intensidad -->
        <div style="background:#1a1a1a;border-radius:8px;padding:10px;margin-bottom:15px;">
            <div style="display:flex;justify-content:space-between;margin-bottom:5px;">
                <span style="color:#888;font-size:0.9em;">Intensidad de movimiento</span>
                <span id="intensityPercent" style="color:#22c55e;font-weight:bold;">0%</span>
            </div>
            <div style="background:#333;border-radius:4px;height:20px;overflow:hidden;">
                <div id="intensityBar" style="height:100%;width:0%;background:linear-gradient(90deg,#22c55e,#f59e0b,#ef4444);transition:width 0.3s;"></div>
            </div>
        </div>
        
        <!-- Forma de onda radar -->
        <div style="background:#0a0a0a;border-radius:8px;padding:10px;margin-bottom:15px;">
            <div style="color:#888;font-size:0.9em;margin-bottom:5px;">📡 Forma de onda radar (últimos 12s)</div>
            <canvas id="waveformCanvas" width="600" height="100" style="width:100%;border-radius:4px;background:#111;"></canvas>
        </div>
        
        <!-- Eventos clasificados -->
        <div style="display:flex;gap:15px;flex-wrap:wrap;margin-bottom:15px;">
            <div style="flex:1;min-width:100px;text-align:center;padding:10px;background:#0a0a0a;border-radius:8px;">
                <div style="font-size:1.5em;font-weight:bold;color:#22c55e;" id="passCount">0</div>
                <div style="color:#888;font-size:0.8em;">🚶 Pasos</div>
            </div>
            <div style="flex:1;min-width:100px;text-align:center;padding:10px;background:#0a0a0a;border-radius:8px;">
                <div style="font-size:1.5em;font-weight:bold;color:#f59e0b;" id="stayCount">0</div>
                <div style="color:#888;font-size:0.8em;">🧍 Permanencia</div>
            </div>
            <div style="flex:1;min-width:100px;text-align:center;padding:10px;background:#0a0a0a;border-radius:8px;">
                <div style="font-size:1.5em;font-weight:bold;color:#ef4444;" id="burstCount">0</div>
                <div style="color:#888;font-size:0.8em;">🔥 Actividad</div>
            </div>
            <div style="flex:1;min-width:100px;text-align:center;padding:10px;background:#0a0a0a;border-radius:8px;">
                <div style="font-size:1.5em;font-weight:bold;color:#3b82f6;" id="totalEvents">0</div>
                <div style="color:#888;font-size:0.8em;">📊 Total</div>
            </div>
        </div>
        
        <!-- Timeline de eventos -->
        <div style="background:#0a0a0a;border-radius:8px;padding:10px;">
            <div style="color:#888;font-size:0.9em;margin-bottom:5px;">📈 Timeline de eventos</div>
            <canvas id="eventTimeline" width="600" height="60" style="width:100%;border-radius:4px;background:#111;"></canvas>
        </div>
        
        <div id="presenceInfo" style="margin-top:10px;color:#888;font-size:0.9em;">Cargando...</div>
    </div>

    <div class="section">
        <h2>📡 Espectro WiFi 2.4GHz</h2>
        <canvas id="spectrumCanvas" width="600" height="200" style="width:100%;border-radius:8px;background:#111;"></canvas>
        <div id="wifiInfo" style="margin-top:10px;color:#888;font-size:0.9em;">Escaneando...</div>
    </div>

    <div class="section">
        <h2>📋 Redes Detectadas</h2>
        <ul class="device-list" id="wifiList"></ul>
    </div>

    <div class="section">
        <h2>🚨 Alertas</h2>
        <div class="alerts" id="alerts">
            <div class="alert-item">
                <div>Sistema iniciado correctamente</div>
                <div class="alert-time">Ahora</div>
            </div>
        </div>
    </div>

    <button class="refresh-btn" id="refreshBtn" onclick="refreshAll()" aria-label="Actualizar todos los datos">🔄</button>

    <!-- Indicador de conexión WebSocket -->
    <div id="wsIndicator" style="position:fixed;top:10px;right:10px;padding:5px 10px;border-radius:12px;font-size:0.75em;z-index:1000;display:none;">● Conectado</div>

    <!-- Toast de errores -->
    <div id="errorToast" style="position:fixed;bottom:80px;left:50%;transform:translateX(-50%);background:#ef4444;color:#fff;padding:10px 20px;border-radius:8px;display:none;z-index:1000;font-size:0.9em;"></div>

    <script>
        // Utilidad: sanitizar texto para prevenir XSS
        function esc(str) {
            if (str == null) return '';
            return String(str).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;');
        }

        // Utilidad: mostrar error en UI
        function showError(msg) {
            const toast = document.getElementById('errorToast');
            toast.textContent = '⚠️ ' + msg;
            toast.style.display = 'block';
            setTimeout(() => toast.style.display = 'none', 4000);
        }

        // Utilidad: mostrar estado vacío
        function emptyState(icon, text) {
            return `<li style="justify-content:center;color:#555;padding:20px;">${icon} ${text}</li>`;
        }

        async function fetchDashboard() {
            try {
                const res = await fetch('/api/dashboard');
                if (!res.ok) throw new Error('HTTP ' + res.status);
                const d = await res.json();
                document.getElementById('motion').textContent = d.motion ? 'SÍ' : 'No';
                document.getElementById('motionCount').textContent = d.motionCount;
                document.getElementById('motionCard').className = 'card' + (d.motion ? ' alert' : '');

                const stat = document.getElementById('status');
                if (d.alertActive) {
                    stat.textContent = '⚠️ ALERTA';
                    stat.className = 'status status-alert';
                } else {
                    stat.textContent = '● Sistema Activo';
                    stat.className = 'status status-ok';
                }

                document.getElementById('devices').textContent = d.devicesOnNetwork;
                document.getElementById('rssi').textContent = d.wifiRSSI;
                document.getElementById('temperature').textContent = d.temperature.toFixed(1) + '°C';
                document.getElementById('humidity').textContent = d.humidity.toFixed(1) + '%';
            } catch (e) { showError('Error cargando dashboard'); console.error(e); }
        }

        async function fetchNetwork() {
            try {
                const res = await fetch('/api/network');
                if (!res.ok) throw new Error('HTTP ' + res.status);
                const d = await res.json();
                document.getElementById('networkInfo').textContent =
                    `Dispositivos: ${d.devices.length} | Nuevos: ${d.newDevices}`;
                const list = document.getElementById('deviceList');
                if (d.devices.length === 0) {
                    list.innerHTML = emptyState('🔒', 'No se detectaron dispositivos');
                    return;
                }
                list.innerHTML = '';
                d.devices.forEach(dev => {
                    const cls = dev.trusted ? 'trust-ok' : dev.new ? 'trust-new' : 'trust-suspect';
                    const txt = dev.trusted ? 'Confiable' : dev.new ? 'Nuevo' : 'Desconocido';
                    list.innerHTML += `<li><div><div class="device-name">${esc(dev.name)}</div><div class="device-ip">${esc(dev.ip)}</div></div><span class="device-trust ${cls}">${txt}</span></li>`;
                });
            } catch (e) { showError('Error cargando red'); console.error(e); }
        }

        // Heatmap Drawing
        function drawHeatmap(canvasId, grid, type) {
            const canvas = document.getElementById(canvasId);
            if (!canvas) return;
            const ctx = canvas.getContext('2d');
            const W = canvas.width;
            const H = canvas.height;
            ctx.clearRect(0, 0, W, H);
            ctx.fillStyle = '#111';
            ctx.fillRect(0, 0, W, H);
            
            if (!grid || grid.length === 0) {
                ctx.fillStyle = '#555';
                ctx.font = '14px sans-serif';
                ctx.textAlign = 'center';
                ctx.fillText('Sin datos', W/2, H/2);
                return;
            }
            
            const rows = grid.length;
            const cols = grid[0].length;
            const cellW = W / cols;
            const cellH = H / rows;
            
            for (let r = 0; r < rows; r++) {
                for (let c = 0; c < cols; c++) {
                    const x = c * cellW;
                    const y = r * cellH;
                    
                    let color;
                    if (type === 'rssi') {
                        const rssi = grid[r][c];
                        const t = Math.max(0, Math.min(1, (rssi + 100) / 70));
                        const red = Math.round(255 * (1 - t));
                        const green = Math.round(200 * t);
                        color = `rgb(${red}, ${green}, 50)`;
                    } else {
                        const cell = grid[r][c];
                        if (!cell.active) {
                            color = 'rgba(50, 50, 50, 0.3)';
                        } else {
                            const intensity = Math.min(1, cell.hits / 10);
                            color = `rgba(59, 130, 246, ${0.3 + intensity * 0.7})`;
                        }
                    }
                    
                    ctx.fillStyle = color;
                    ctx.fillRect(x + 1, y + 1, cellW - 2, cellH - 2);
                    
                    ctx.fillStyle = 'rgba(255,255,255,0.7)';
                    ctx.font = '10px sans-serif';
                    ctx.textAlign = 'center';
                    ctx.fillText(type === 'rssi' ? grid[r][c] : grid[r][c].hits, x + cellW/2, y + cellH/2 + 3);
                }
            }
            
            ctx.strokeStyle = 'rgba(255,255,255,0.1)';
            ctx.lineWidth = 0.5;
            for (let r = 0; r <= rows; r++) {
                ctx.beginPath();
                ctx.moveTo(0, r * cellH);
                ctx.lineTo(W, r * cellH);
                ctx.stroke();
            }
            for (let c = 0; c <= cols; c++) {
                ctx.beginPath();
                ctx.moveTo(c * cellW, 0);
                ctx.lineTo(c * cellW, H);
                ctx.stroke();
            }
        }
        
        async function fetchHeatmap() {
            try {
                const res = await fetch('/api/heatmap');
                if (!res.ok) throw new Error('HTTP ' + res.status);
                const d = await res.json();
                
                drawHeatmap('wifiHeatmap', d.rssiGrid, 'rssi');
                drawHeatmap('radarHeatmap', d.presenceGrid, 'presence');
                
                document.getElementById('heatmapInfo').textContent = 
                    `Red: ${d.ourRSSI} dBm | Canal: ${d.ourChannel} | Redes: ${d.totalNetworks} | Grid: 8x6`;
            } catch (e) { showError('Error cargando heatmaps'); console.error(e); }
        }
        
        // Draw waveform
        function drawWaveform(data) {
            const canvas = document.getElementById('waveformCanvas');
            if (!canvas) return;
            const ctx = canvas.getContext('2d');
            const W = canvas.width;
            const H = canvas.height;
            ctx.clearRect(0, 0, W, H);
            ctx.fillStyle = '#111';
            ctx.fillRect(0, 0, W, H);
            
            // Línea de tiempo
            ctx.strokeStyle = '#333';
            ctx.beginPath();
            ctx.moveTo(0, H/2);
            ctx.lineTo(W, H/2);
            ctx.stroke();
            
            // Forma de onda
            ctx.strokeStyle = '#3b82f6';
            ctx.lineWidth = 2;
            ctx.beginPath();
            
            const step = W / data.length;
            data.forEach((val, i) => {
                const x = i * step;
                const y = H - (val / 255) * H;
                if (i === 0) ctx.moveTo(x, y);
                else ctx.lineTo(x, y);
            });
            ctx.stroke();
            
            // Rellenar debajo
            ctx.lineTo(W, H);
            ctx.lineTo(0, H);
            ctx.closePath();
            ctx.fillStyle = 'rgba(59, 130, 246, 0.2)';
            ctx.fill();
            
            // Etiquetas
            ctx.fillStyle = '#888';
            ctx.font = '10px sans-serif';
            ctx.fillText('12s', 5, 12);
            ctx.fillText('6s', W/2 - 5, 12);
            ctx.fillText('ahora', W - 30, 12);
            ctx.fillText('HIGH', 5, H - 5);
            ctx.fillText('LOW', 5, 15);
        }
        
        // Draw event timeline
        function drawEventTimeline(events) {
            const canvas = document.getElementById('eventTimeline');
            if (!canvas) return;
            const ctx = canvas.getContext('2d');
            const W = canvas.width;
            const H = canvas.height;
            ctx.clearRect(0, 0, W, H);
            ctx.fillStyle = '#111';
            ctx.fillRect(0, 0, W, H);
            
            if (events.length === 0) return;
            
            const barW = W / events.length;
            events.forEach((evt, i) => {
                const x = i * barW;
                const h = Math.min(H - 5, evt.d / 100);
                
                let color = '#333';
                if (evt.type === 'pass') color = '#22c55e';
                else if (evt.type === 'stay') color = '#f59e0b';
                else if (evt.type === 'burst') color = '#ef4444';
                
                ctx.fillStyle = color;
                ctx.fillRect(x, H - h - 2, barW - 1, h);
            });
            
            // Leyenda
            ctx.fillStyle = '#22c55e'; ctx.fillRect(5, 5, 8, 8);
            ctx.fillStyle = '#888'; ctx.font = '9px sans-serif'; ctx.fillText('Paso', 15, 12);
            ctx.fillStyle = '#f59e0b'; ctx.fillRect(45, 5, 8, 8);
            ctx.fillStyle = '#888'; ctx.fillText('Estancia', 55, 12);
            ctx.fillStyle = '#ef4444'; ctx.fillRect(100, 5, 8, 8);
            ctx.fillStyle = '#888'; ctx.fillText('Activo', 110, 12);
        }
        
        // Presence Detection PRO
        async function fetchPresence() {
            try {
                const [resState, resWave, resHist] = await Promise.all([
                    fetch('/api/presence'),
                    fetch('/api/presence/waveform'),
                    fetch('/api/presence/history')
                ]);
                
                const d = await resState.json();
                const wave = await resWave.json();
                const history = await resHist.json();
                
                // Estado
                const icon = document.getElementById('presenceIcon');
                const status = document.getElementById('presenceStatus');
                if (d.radarActive) {
                    icon.textContent = '🏃';
                    status.textContent = 'MOVIMIENTO';
                    status.style.color = '#ef4444';
                } else if (d.present) {
                    icon.textContent = '🧑';
                    status.textContent = 'Presencia';
                    status.style.color = '#22c55e';
                } else {
                    icon.textContent = '👤';
                    status.textContent = 'Ausente';
                    status.style.color = '#888';
                }
                
                document.getElementById('presencePeople').textContent = d.people;
                document.getElementById('presenceDuration').textContent = d.eventDuration + 'ms';
                
                // Intensidad
                const intensity = Math.round(d.intensity * 100);
                document.getElementById('presenceIntensity').textContent = intensity + '%';
                document.getElementById('intensityPercent').textContent = intensity + '%';
                document.getElementById('intensityBar').style.width = intensity + '%';
                
                // Eventos clasificados
                document.getElementById('passCount').textContent = d.passes;
                document.getElementById('stayCount').textContent = d.stays;
                document.getElementById('burstCount').textContent = d.bursts;
                document.getElementById('totalEvents').textContent = d.totalEvents;
                
                // Info
                document.getElementById('presenceInfo').textContent = 
                    `Score: ${d.score} | WiFi: ${d.wifiDevices} dispositivos`;
                
                drawWaveform(wave);
                drawEventTimeline(history);
            } catch (e) { showError('Error cargando presencia'); console.error(e); }
        }
        
        // WiFi Spectrum
        async function fetchWiFi() {
            try {
                const res = await fetch('/api/wifi');
                if (!res.ok) throw new Error('HTTP ' + res.status);
                const d = await res.json();
                
                // Dibujar espectro
                const canvas = document.getElementById('spectrumCanvas');
                const ctx = canvas.getContext('2d');
                const W = canvas.width;
                const H = canvas.height;
                ctx.clearRect(0, 0, W, H);
                
                // Fondo
                ctx.fillStyle = '#111';
                ctx.fillRect(0, 0, W, H);
                
                // Dibujar canales 1-13
                const barWidth = W / 14;
                const maxCount = Math.max(...d.channels, 1);
                
                for (let ch = 1; ch <= 13; ch++) {
                    const count = d.channels[ch];
                    const barHeight = (count / maxCount) * (H - 40);
                    const x = ch * barWidth;
                    const y = H - 20 - barHeight;
                    
                    // Color según congestión
                    let color = '#22c55e';  // Verde = libre
                    if (count >= 3) color = '#ef4444';  // Rojo = congestión
                    else if (count >= 2) color = '#f59e0b';  // Amarillo = moderado
                    
                    // Canal actual más brillante
                    if (ch === d.ourChannel) {
                        ctx.fillStyle = '#3b82f6';
                        ctx.fillRect(x - 2, y - 2, barWidth - 4, barHeight + 4);
                    }
                    
                    ctx.fillStyle = color;
                    ctx.fillRect(x + 2, y, barWidth - 4, barHeight);
                    
                    // Número de canal
                    ctx.fillStyle = '#888';
                    ctx.font = '11px sans-serif';
                    ctx.textAlign = 'center';
                    ctx.fillText(ch, x + barWidth/2, H - 5);
                    
                    // Conteo encima
                    if (count > 0) {
                        ctx.fillStyle = '#fff';
                        ctx.fillText(count, x + barWidth/2, y - 5);
                    }
                }
                
                // Leyenda
                ctx.fillStyle = '#22c55e'; ctx.fillRect(W - 180, 10, 12, 12);
                ctx.fillStyle = '#888'; ctx.font = '11px sans-serif'; ctx.textAlign = 'left';
                ctx.fillText('Libre', W - 165, 20);
                ctx.fillStyle = '#f59e0b'; ctx.fillRect(W - 120, 10, 12, 12);
                ctx.fillStyle = '#888'; ctx.fillText('Medio', W - 105, 20);
                ctx.fillStyle = '#ef4444'; ctx.fillRect(W - 60, 10, 12, 12);
                ctx.fillStyle = '#888'; ctx.fillText('Alto', W - 45, 20);
                
                document.getElementById('wifiInfo').textContent = 
                    `Red: ${d.ourSSID} | Canal: ${d.ourChannel} | RSSI: ${d.ourRSSI} dBm | Total redes: ${d.total}`;
                
                const list = document.getElementById('wifiList');
                if (!d.networks || d.networks.length === 0) {
                    list.innerHTML = emptyState('📡', 'No se detectaron redes WiFi');
                    return;
                }
                list.innerHTML = '';
                d.networks.sort((a, b) => b.rssi - a.rssi).forEach(net => {
                    const sigColor = net.rssi > -50 ? '#22c55e' : net.rssi > -70 ? '#f59e0b' : '#ef4444';
                    list.innerHTML += `<li>
                        <div>
                            <div class="device-name">${esc(net.ssid) || 'Oculta'}</div>
                            <div class="device-ip">Ch ${net.channel} | ${esc(net.bssid)} | ${net.secure ? '🔒' : '🔓'}</div>
                        </div>
                        <span style="color:${sigColor};font-weight:bold;">${net.rssi} dBm</span>
                    </li>`;
                });
            } catch (e) { showError('Error cargando WiFi'); console.error(e); }
            } catch (e) { console.error(e); }
        }
        
        // WebSocket para tiempo real
        let ws;
        let wsConnected = false;
        function updateWSIndicator(connected) {
            wsConnected = connected;
            const ind = document.getElementById('wsIndicator');
            ind.style.display = 'block';
            if (connected) {
                ind.textContent = '● Conectado';
                ind.style.background = '#22c55e33';
                ind.style.color = '#22c55e';
            } else {
                ind.textContent = '● Desconectado';
                ind.style.background = '#ef444433';
                ind.style.color = '#ef4444';
            }
            setTimeout(() => ind.style.display = 'none', 3000);
        }
        function connectWS() {
            ws = new WebSocket('ws://' + location.host + '/ws');
            ws.onopen = function() { updateWSIndicator(true); };
            ws.onmessage = function(evt) {
                const data = evt.data;
                if (data.startsWith('W:')) {
                    drawWaveform(JSON.parse(data.substring(2)));
                } else {
                    const d = JSON.parse(data);
                    const icon = document.getElementById('presenceIcon');
                    const status = document.getElementById('presenceStatus');
                    if (d.radarActive) {
                        icon.textContent = '🏃';
                        status.textContent = 'MOVIMIENTO';
                        status.style.color = '#ef4444';
                    } else if (d.present) {
                        icon.textContent = '🧑';
                        status.textContent = 'Presencia';
                        status.style.color = '#22c55e';
                    } else {
                        icon.textContent = '👤';
                        status.textContent = 'Ausente';
                        status.style.color = '#888';
                    }
                    document.getElementById('presencePeople').textContent = d.people;
                    document.getElementById('presenceDuration').textContent = d.eventDuration + 'ms';
                    const intensity = Math.round(d.intensity * 100);
                    document.getElementById('presenceIntensity').textContent = intensity + '%';
                    document.getElementById('intensityPercent').textContent = intensity + '%';
                    document.getElementById('intensityBar').style.width = intensity + '%';
                    document.getElementById('passCount').textContent = d.passes;
                    document.getElementById('stayCount').textContent = d.stays;
                    document.getElementById('burstCount').textContent = d.bursts;
                    document.getElementById('totalEvents').textContent = d.totalEvents;
                    document.getElementById('presenceInfo').textContent = `Score: ${d.score} | WiFi: ${d.wifiDevices}`;
                }
            };
            ws.onclose = function() { updateWSIndicator(false); setTimeout(connectWS, 2000); };
            ws.onerror = function() { updateWSIndicator(false); };
        }
        connectWS();
        
        // Network Scanner
        async function fetchScan() {
            try {
                const [resDevices, resStats] = await Promise.all([
                    fetch('/api/scan'),
                    fetch('/api/scan/stats')
                ]);
                const devices = await resDevices.json();
                const stats = await resStats.json();
                
                document.getElementById('scanDevices').textContent = stats.devices;
                document.getElementById('scanPorts').textContent = stats.totalOpenPorts;
                document.getElementById('scanHighRisk').textContent = stats.highRisk;
                document.getElementById('scanMedRisk').textContent = stats.medRisk;
                document.getElementById('scanStatus').textContent = 
                    `Último escaneo: ${stats.lastScan}s | ${stats.scanComplete ? '✅ Completo' : '🔄 Escaneando...'}`;
                
                const riskColors = ['#22c55e', '#f59e0b', '#ef4444', '#dc2626'];
                const riskLabels = ['✅ Seguro', '⚠️ Bajo', '⚠️ Medio', '🔴 ALTO'];
                const resultsDiv = document.getElementById('scanResults');
                
                if (devices.length === 0) {
                    resultsDiv.innerHTML = emptyState('🔍', 'Sin resultados. Presiona Escanear para buscar dispositivos');
                    return;
                }
                
                let html = '';
                devices.forEach(dev => {
                    html += `<div style="background:#0a0a0a;border-radius:8px;padding:15px;margin-bottom:10px;border-left:4px solid ${riskColors[dev.riskLevel]};">`;
                    html += `<div style="display:flex;justify-content:space-between;align-items:center;">
                        <div>
                            <div style="font-weight:bold;font-size:1.1em;">${esc(dev.ip)}</div>
                            <div style="color:#888;font-size:0.9em;">${esc(dev.vendor)} | ${esc(dev.mac)}</div>
                        </div>
                        <span style="color:${riskColors[dev.riskLevel]};font-weight:bold;">${riskLabels[dev.riskLevel]}</span>
                    </div>`;
                    
                    if (dev.openPorts > 0) {
                        html += `<div style="margin-top:10px;">`;
                        dev.ports.forEach(port => {
                            const vulnColor = port.vuln ? '#ef4444' : '#888';
                            html += `<div style="display:flex;justify-content:space-between;padding:5px 0;border-top:1px solid #333;">
                                <span>🔓 <b>${port.port}</b> (${esc(port.service)})</span>
                                <span style="color:${vulnColor};font-size:0.85em;">${esc(port.vuln) || 'OK'}</span>
                            </div>`;
                        });
                        html += `</div>`;
                    }
                    
                    html += `</div>`;
                });
                
                resultsDiv.innerHTML = html;
            } catch (e) { showError('Error cargando scanner'); console.error(e); }
        }
        
        let scanning = false;
        async function startScan() {
            if (scanning) return;
            scanning = true;
            const btn = document.getElementById('scanBtn');
            const origText = btn.innerHTML;
            btn.innerHTML = '⏳ Escaneando...';
            btn.disabled = true;
            btn.style.opacity = '0.6';
            document.getElementById('scanStatus').textContent = '🔄 Escaneando... esto puede tardar 30-60 segundos';
            try {
                await fetch('/api/scan/start');
                setTimeout(() => { fetchScan(); scanning = false; btn.innerHTML = origText; btn.disabled = false; btn.style.opacity = '1'; }, 35000);
            } catch (e) { 
                showError('Error al iniciar escaneo'); 
                scanning = false; 
                btn.innerHTML = origText; 
                btn.disabled = false; 
                btn.style.opacity = '1'; 
            }
        }
        
        // Refresh ALL data
        async function refreshAll() {
            const btn = document.getElementById('refreshBtn');
            btn.style.transform = 'rotate(360deg)';
            btn.style.transition = 'transform 0.5s';
            await Promise.allSettled([
                fetchDashboard(), fetchNetwork(), fetchWiFi(),
                fetchPresence(), fetchHeatmap(), fetchScan()
            ]);
            setTimeout(() => { btn.style.transform = ''; btn.style.transition = ''; }, 500);
        }
        
        // HTTP polling
        setInterval(fetchDashboard, 3000);
        setInterval(fetchNetwork, 10000);
        setInterval(fetchScan, 30000);
        setInterval(fetchWiFi, 15000);
        setInterval(fetchPresence, 2000);
        setInterval(fetchHeatmap, 10000);
        fetchDashboard();
        fetchNetwork();
        fetchWiFi();
        fetchPresence();
        fetchHeatmap();
        fetchScan();
    </script>
</body>
</html>
)rawliteral";
        request->send(200, "text/html", html);
    });

    // API Dashboard combinada (data + stats en uno solo)
    server.on("/api/dashboard", HTTP_GET, [](AsyncWebServerRequest *request) {
        JsonDocument doc;
        doc["motion"] = motionDetected;
        doc["motionCount"] = motionCount;
        doc["alertActive"] = guardian.hasNewDevice();
        doc["uptime"] = millis() / 1000;
        doc["freeHeap"] = ESP.getFreeHeap();
        doc["wifiRSSI"] = WiFi.RSSI();
        doc["devicesOnNetwork"] = guardian.getDeviceCount();
        doc["stealthMode"] = guardian.isStealthMode();
        doc["temperature"] = currentTemp;
        doc["humidity"] = currentHumidity;
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    // API datos (legacy - mantener compatibilidad)
    server.on("/api/data", HTTP_GET, [](AsyncWebServerRequest *request) {
        JsonDocument doc;
        doc["motion"] = motionDetected;
        doc["motionCount"] = motionCount;
        doc["alertActive"] = guardian.hasNewDevice();
        doc["uptime"] = millis() / 1000;
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    // API red
    server.on("/api/network", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", guardian.getDevicesJSON());
    });

    // API alertas
    server.on("/api/alerts", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", guardian.getAlertsJSON());
    });

    // API stats (legacy - mantener compatibilidad)
    server.on("/api/stats", HTTP_GET, [](AsyncWebServerRequest *request) {
        JsonDocument doc;
        doc["uptime"] = millis() / 1000;
        doc["freeHeap"] = ESP.getFreeHeap();
        doc["wifiRSSI"] = WiFi.RSSI();
        doc["devicesOnNetwork"] = guardian.getDeviceCount();
        doc["stealthMode"] = guardian.isStealthMode();
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    // API agente
    server.on("/api/agent", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", guardian.getAgentStatus());
    });

    // API Scanner - Dispositivos
    server.on("/api/scan", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", scanner.getDevicesJSON());
    });
    
    // API Scanner - Stats
    server.on("/api/scan/stats", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", scanner.getStatsJSON());
    });
    
    // API Scanner - Trigger scan (non-blocking)
    server.on("/api/scan/start", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", "{\"status\":\"started\"}");
        // Ejecutar scan después de responder
        static bool scanning = false;
        if (!scanning) {
            scanning = true;
            // Usar timer para no bloquear
            xTaskCreate([](void*) {
                scanner.fullScan();
                scanning = false;
                vTaskDelete(NULL);
            }, "scan", 10240, NULL, 2, NULL);
        }
    });
    
    // API Heatmap
    server.on("/api/heatmap", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", heatmap.getFullJSON());
    });
    
    // API Presencia PRO
    server.on("/api/presence", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", presence.getStateJSON());
    });
    
    // API Forma de onda radar
    server.on("/api/presence/waveform", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", presence.getWaveformJSON());
    });
    
    // API Historial de eventos
    server.on("/api/presence/history", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", presence.getHistoryJSON());
    });
    
    // API WiFi Spectrum - usa cache
    server.on("/api/wifi", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", cachedWifiJson);
    });
}
