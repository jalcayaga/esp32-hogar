/**
 * 🗺️ Heatmap Generator
 * 
 * Genera mapas de calor para:
 * - WiFi: fuerza de señal (RSSI) por zona del grid
 * - Radar: presencia/movimiento por zona
 * 
 * Grid configurable (default 8x6 = 48 celdas)
 */

#ifndef HEATMAP_H
#define HEATMAP_H

#include <Arduino.h>
#include <WiFi.h>

#define GRID_COLS 8
#define GRID_ROWS 6
#define MAX_WIFI_NETWORKS 20

// Celda del grid
struct Cell {
    int8_t rssi;           // RSSI promedio (-100 a 0)
    uint8_t deviceCount;   // Dispositivos en esta zona
    bool radarActive;      // Radar activo en esta zona
    uint8_t radarHits;     // Hits de radar
    unsigned long lastUpdate;
};

// Red WiFi detectada
struct WifiNetwork {
    char ssid[33];
    int8_t rssi;
    uint8_t channel;
    char bssid[18];
    bool secure;
};

class HeatmapGenerator {
private:
    Cell grid[GRID_ROWS][GRID_COLS];
    WifiNetwork networks[MAX_WIFI_NETWORKS];
    int networkCount = 0;
    int currentCellRow = 0;
    int currentCellCol = 0;
    
    // Nuestro RSSI de referencia
    int8_t ourRSSI = -50;
    int ourChannel = 1;

public:
    void begin() {
        // Inicializar grid
        for (int r = 0; r < GRID_ROWS; r++) {
            for (int c = 0; c < GRID_COLS; c++) {
                grid[r][c].rssi = -100;
                grid[r][c].deviceCount = 0;
                grid[r][c].radarActive = false;
                grid[r][c].radarHits = 0;
                grid[r][c].lastUpdate = 0;
            }
        }
        Serial.println("🗺️ Heatmap Generator iniciado");
    }

    // Escanear WiFi y actualizar grid
    void scanWifi() {
        int n = WiFi.scanNetworks();
        networkCount = min(n, MAX_WIFI_NETWORKS);
        
        ourRSSI = WiFi.RSSI();
        ourChannel = WiFi.channel();
        
        for (int i = 0; i < networkCount; i++) {
            strncpy(networks[i].ssid, WiFi.SSID(i).c_str(), 32);
            networks[i].ssid[32] = '\0';
            networks[i].rssi = WiFi.RSSI(i);
            networks[i].channel = WiFi.channel(i);
            strncpy(networks[i].bssid, WiFi.BSSIDstr(i).c_str(), 17);
            networks[i].bssid[17] = '\0';
            networks[i].secure = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
        }
        
        WiFi.scanDelete();
        
        // Simular variación espacial usando BSSID como hash
        // En un escenario real, moverías el ESP32 por el espacio
        updateGridWithNetworks();
    }

    // Actualizar grid con datos de redes
    void updateGridWithNetworks() {
        unsigned long now = millis();
        
        for (int r = 0; r < GRID_ROWS; r++) {
            for (int c = 0; c < GRID_COLS; c++) {
                // Simular variación espacial
                // Usar la posición del grid + BSSID como seed para variación
                float variation = sin((r * GRID_COLS + c) * 0.7) * 15;
                
                // RSSI base con variación por celda
                int8_t baseRSSI = ourRSSI + (int8_t)variation;
                
                // Ajustar según cantidad de redes en ese "canal virtual"
                int channelOffset = (c % 13) + 1;
                int networksInChannel = 0;
                for (int i = 0; i < networkCount; i++) {
                    if (networks[i].channel == channelOffset) {
                        networksInChannel++;
                        // Red más fuerte domina
                        if (networks[i].rssi > baseRSSI) {
                            baseRSSI = networks[i].rssi;
                        }
                    }
                }
                
                // Promediar con valor anterior (suavizado)
                if (grid[r][c].lastUpdate > 0) {
                    grid[r][c].rssi = (grid[r][c].rssi + baseRSSI) / 2;
                } else {
                    grid[r][c].rssi = baseRSSI;
                }
                
                grid[r][c].deviceCount = networksInChannel;
                grid[r][c].lastUpdate = now;
            }
        }
    }

    // Actualizar radar para una celda específica
    void updateRadar(int row, int col, bool active) {
        if (row >= 0 && row < GRID_ROWS && col >= 0 && col < GRID_COLS) {
            grid[row][col].radarActive = active;
            if (active) {
                grid[row][col].radarHits++;
            }
        }
    }

    // Simular presencia en el grid basado en el radar real
    void simulatePresence(bool radarDetected) {
        // Distribuir la presencia en celdas cercanas al centro
        int centerR = GRID_ROWS / 2;
        int centerC = GRID_COLS / 2;
        
        for (int r = 0; r < GRID_ROWS; r++) {
            for (int c = 0; c < GRID_COLS; c++) {
                float dist = sqrt((r - centerR) * (r - centerR) + (c - centerC) * (c - centerC));
                float prob = max(0.0f, 1.0f - dist / 4.0f);
                
                if (radarDetected && random(100) < (int)(prob * 70)) {
                    grid[r][c].radarActive = true;
                    grid[r][c].radarHits++;
                } else {
                    grid[r][c].radarActive = false;
                }
            }
        }
    }

    // ==================== GETTERS ====================

    // RSSI a color (verde=fuerte, rojo=débil)
    static uint16_t rssiToColor(int8_t rssi) {
        if (rssi > -50) return 0x07E0;      // Verde brillante
        if (rssi > -60) return 0x07E0;      // Verde
        if (rssi > -70) return 0x7FE0;      // Verde-amarillo
        if (rssi > -80) return 0xFFE0;      // Amarillo
        if (rssi > -90) return 0xFD20;      // Naranja
        return 0xF800;                       // Rojo
    }

    // Presencia a color
    static uint16_t presenceToColor(bool active, uint8_t hits) {
        if (!active) return 0x2104;          // Gris oscuro
        if (hits > 10) return 0xF800;        // Rojo (alta actividad)
        if (hits > 5) return 0xFFE0;         // Amarillo
        return 0x07FF;                       // Cyan
    }

    // JSON para API - Grid RSSI
    String getRssiGridJSON() {
        String json = "[";
        for (int r = 0; r < GRID_ROWS; r++) {
            if (r > 0) json += ",";
            json += "[";
            for (int c = 0; c < GRID_COLS; c++) {
                if (c > 0) json += ",";
                json += String(grid[r][c].rssi);
            }
            json += "]";
        }
        json += "]";
        return json;
    }

    // JSON para API - Grid presencia
    String getPresenceGridJSON() {
        String json = "[";
        for (int r = 0; r < GRID_ROWS; r++) {
            if (r > 0) json += ",";
            json += "[";
            for (int c = 0; c < GRID_COLS; c++) {
                if (c > 0) json += ",";
                json += "{";
                json += "\"active\":" + String(grid[r][c].radarActive ? "1" : "0") + ",";
                json += "\"hits\":" + String(grid[r][c].radarHits);
                json += "}";
            }
            json += "]";
        }
        json += "]";
        return json;
    }

    // JSON para API - Redes
    String getNetworksJSON() {
        String json = "[";
        for (int i = 0; i < networkCount; i++) {
            if (i > 0) json += ",";
            json += "{";
            json += "\"ssid\":\"" + String(networks[i].ssid) + "\",";
            json += "\"rssi\":" + String(networks[i].rssi) + ",";
            json += "\"channel\":" + String(networks[i].channel) + ",";
            json += "\"bssid\":\"" + String(networks[i].bssid) + "\",";
            json += "\"secure\":" + String(networks[i].secure ? "true" : "false");
            json += "}";
        }
        json += "]";
        return json;
    }

    // JSON completo
    String getFullJSON() {
        String json = "{";
        json += "\"rssiGrid\":" + getRssiGridJSON() + ",";
        json += "\"presenceGrid\":" + getPresenceGridJSON() + ",";
        json += "\"networks\":" + getNetworksJSON() + ",";
        json += "\"ourRSSI\":" + String(ourRSSI) + ",";
        json += "\"ourChannel\":" + String(ourChannel) + ",";
        json += "\"totalNetworks\":" + String(networkCount);
        json += "}";
        return json;
    }
};

#endif // HEATMAP_H
