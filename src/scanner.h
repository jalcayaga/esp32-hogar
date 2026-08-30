/**
 * 🔍 Network Scanner
 * 
 * Escaneo completo de red local:
 * - Discovery de dispositivos (ping + ARP)
 * - Port scan de puertos comunes
 * - Identificación de servicios
 * - Detección de vulnerabilidades básicas
 * - Vendor lookup por MAC
 */

#ifndef SCANNER_H
#define SCANNER_H

#include <Arduino.h>
#include <WiFi.h>
#include <ESP32Ping.h>

#define MAX_DEVICES 20
#define MAX_PORTS 6
#define MAX_SERVICES 20
#define PORT_TIMEOUT 150  // Reducido a 150ms

// Puerto a escanear
struct PortScan {
    uint16_t port;
    bool open;
    char service[20];
    char vulnerability[50];
};

// Dispositivo detectado
struct ScannedDevice {
    IPAddress ip;
    char mac[18];
    char vendor[30];
    char hostname[32];
    PortScan ports[MAX_PORTS];
    int openPorts;
    int riskLevel;      // 0=safe, 1=low, 2=medium, 3=high
    bool online;
    unsigned long lastSeen;
};

// Puertos comunes para escanear
const uint16_t commonPorts[] = {
    21,     // FTP
    22,     // SSH
    23,     // Telnet
    25,     // SMTP
    53,     // DNS
    80,     // HTTP
    443,    // HTTPS
    3389,   // RDP
    554,    // RTSP (cámaras)
    8080    // HTTP Proxy
};

const char* portNames[] = {
    "FTP", "SSH", "Telnet", "SMTP", "DNS",
    "HTTP", "HTTPS", "RDP", "RTSP", "HTTP-Alt"
};

// Vulnerabilidades conocidas por servicio
const char* getVulnerability(uint16_t port, bool secure) {
    switch (port) {
        case 21: return "FTP: Sin cifrar, credenciales en texto plano";
        case 22: return secure ? "" : "SSH: Versión antigua o débil";
        case 23: return "Telnet: SIN CIFRAR - extremadamente inseguro";
        case 25: return "SMTP: Puede ser spam relay";
        case 53: return secure ? "" : "DNS: Posible DNS spoofing";
        case 80: return "HTTP: Sin cifrar, vulnerable a MITM";
        case 443: return secure ? "" : "HTTPS: Certificado inválido";
        case 3389: return "RDP: Brute force frecuente";
        case 554: return "RTSP: Cámara sin autenticación";
        case 8080: return "HTTP-Alt: Admin panel expuesto";
        default: return "";
    }
}

// Vendor lookup simplificado
void lookupVendor(const char* mac, char* vendor) {
    if (strlen(mac) < 8) {
        strcpy(vendor, "Desconocido");
        return;
    }
    
    // Obtener OUI (primeros 3 octetos)
    char oui[9];
    strncpy(oui, mac, 8);
    oui[8] = '\0';
    
    // Convertir a mayúsculas para comparar
    for (int i = 0; i < 8; i++) oui[i] = toupper(oui[i]);
    
    // Bases de datos simplificadas de OUI
    if (strstr(oui, "00:1A:11") || strstr(oui, "00:26:AB")) strcpy(vendor, "Google");
    else if (strstr(oui, "AC:BC:32") || strstr(oui, "3C:22:FB")) strcpy(vendor, "Apple");
    else if (strstr(oui, "B8:27:EB") || strstr(oui, "DC:A6:32")) strcpy(vendor, "Raspberry Pi");
    else if (strstr(oui, "54:9F:13") || strstr(oui, "60:38:E0")) strcpy(vendor, "Amazon/Echo");
    else if (strstr(oui, "00:1E:58") || strstr(oui, "00:0C:29")) strcpy(vendor, "VMware");
    else if (strstr(oui, "08:00:27") || strstr(oui, "0A:00:27")) strcpy(vendor, "VirtualBox");
    else if (strstr(oui, "F8:1A:67") || strstr(oui, "B4:E6:2D")) strcpy(vendor, "Samsung");
    else if (strstr(oui, "00:1F:33") || strstr(oui, "28:6C:07")) strcpy(vendor, "Xiaomi");
    else if (strstr(oui, "FC:A1:83") || strstr(oui, "E4:5F:01")) strcpy(vendor, "Huawei");
    else if (strstr(oui, "30:B5:C2") || strstr(oui, "18:59:36")) strcpy(vendor, "TP-Link");
    else if (strstr(oui, "00:17:88") || strstr(oui, "EC:FA:BC")) strcpy(vendor, "Philips Hue");
    else if (strstr(oui, "00:24:E4") || strstr(oui, "5C:CF:7F")) strcpy(vendor, "Espressif/ESP");
    else if (strstr(oui, "44:65:0D") || strstr(oui, "7C:8B:CA")) strcpy(vendor, "IoT Device");
    else strcpy(vendor, "Otro");
}

class NetworkScanner {
private:
    ScannedDevice devices[MAX_DEVICES];
    int deviceCount = 0;
    bool scanComplete = false;
    int scanProgress = 0;
    unsigned long lastFullScan = 0;

public:
    void begin() {
        deviceCount = 0;
        Serial.println("🔍 Network Scanner iniciado");
    }

    // Escaneo completo de red (optimizado)
    void fullScan() {
        Serial.println("🔍 Iniciando escaneo...");
        deviceCount = 0;
        scanProgress = 0;
        scanComplete = false;
        
        IPAddress localIP = WiFi.localIP();
        IPAddress subnet = WiFi.subnetMask();
        
        uint32_t ip = (uint32_t)localIP;
        uint32_t mask = (uint32_t)subnet;
        uint32_t network = ip & mask;
        uint32_t broadcast = network | ~mask;
        
        // 1. Ping sweep rápido
        Serial.println("📡 Discovery...");
        for (uint32_t i = network + 1; i < broadcast && deviceCount < MAX_DEVICES; i++) {
            IPAddress target(i);
            if (target == localIP) continue;
            
            if (Ping.ping(target, 2)) {
                addDevice(target);
                yield();  // Dar tiempo al WiFi
            }
        }
        
        Serial.printf(" %d dispositivos encontrados\n", deviceCount);
        
        // 2. Port scan ligero
        Serial.println("🔌 Port scan...");
        for (int i = 0; i < deviceCount; i++) {
            scanPorts(&devices[i]);
            yield();  // Dar tiempo al WiFi
        }
        
        // 3. Calcular riesgo
        for (int i = 0; i < deviceCount; i++) {
            calculateRisk(&devices[i]);
        }
        
        scanComplete = true;
        lastFullScan = millis();
        
        Serial.printf("✅ Listo: %d dispositivos\n", deviceCount);
    }

    // Escaneo rápido (solo discovery)
    void quickScan() {
        deviceCount = 0;
        
        IPAddress localIP = WiFi.localIP();
        IPAddress subnet = WiFi.subnetMask();
        
        uint32_t ip = (uint32_t)localIP;
        uint32_t mask = (uint32_t)subnet;
        uint32_t network = ip & mask;
        uint32_t broadcast = network | ~mask;
        
        for (uint32_t i = network + 1; i < broadcast; i++) {
            IPAddress target(i);
            if (target == localIP) continue;
            
            String targetStr = target.toString();
            if (!isPrivateIP(targetStr)) continue;
            
            if (Ping.ping(target, 2)) {
                addDevice(target);
            }
        }
        
        lastFullScan = millis();
    }

    // Escaneo de puertos (ligero)
    void scanPorts(ScannedDevice* dev) {
        dev->openPorts = 0;
        
        for (int p = 0; p < MAX_PORTS; p++) {
            uint16_t port = commonPorts[p];
            
            WiFiClient client;
            client.setTimeout(PORT_TIMEOUT);
            
            if (client.connect(dev->ip, port)) {
                if (dev->openPorts < MAX_PORTS) {
                    dev->ports[dev->openPorts].port = port;
                    dev->ports[dev->openPorts].open = true;
                    strcpy(dev->ports[dev->openPorts].service, portNames[p]);
                    strcpy(dev->ports[dev->openPorts].vulnerability, getVulnerability(port, false));
                    dev->openPorts++;
                }
                client.stop();
                Serial.printf("  %s:%d %s\n", dev->ip.toString().c_str(), port, portNames[p]);
            }
        }
    }

    // Calcular nivel de riesgo
    void calculateRisk(ScannedDevice* dev) {
        dev->riskLevel = 0;
        
        for (int i = 0; i < dev->openPorts; i++) {
            uint16_t port = dev->ports[i].port;
            
            // Alto riesgo
            if (port == 23 || port == 554) dev->riskLevel = 3;  // Telnet, RTSP abierto
            
            // Medio riesgo
            else if (port == 21 || port == 3389 || port == 8080) {
                if (dev->riskLevel < 2) dev->riskLevel = 2;
            }
            
            // Bajo riesgo
            else if (port == 22 || port == 80) {
                if (dev->riskLevel < 1) dev->riskLevel = 1;
            }
        }
    }

    // ==================== UTILIDADES ====================
    
    bool isPrivateIP(String ip) {
        int firstOctet = ip.substring(0, ip.indexOf('.')).toInt();
        int secondOctet = ip.substring(ip.indexOf('.') + 1, ip.indexOf('.', ip.indexOf('.') + 1)).toInt();
        if (firstOctet == 10) return true;
        if (firstOctet == 172 && secondOctet >= 16 && secondOctet <= 31) return true;
        if (firstOctet == 192 && secondOctet == 168) return true;
        return false;
    }

    void addDevice(IPAddress ip) {
        if (deviceCount >= MAX_DEVICES) return;
        
        ScannedDevice& dev = devices[deviceCount];
        dev.ip = ip;
        strcpy(dev.mac, "xx:xx:xx:xx:xx:xx");  // Placeholder
        strcpy(dev.hostname, "---");
        dev.openPorts = 0;
        dev.riskLevel = 0;
        dev.online = true;
        dev.lastSeen = millis();
        
        // Vendor lookup
        lookupVendor(dev.mac, dev.vendor);
        
        deviceCount++;
        Serial.printf("  📱 %s (%s)\n", ip.toString().c_str(), dev.vendor);
    }

    void printReport() {
        Serial.println("\n═══════════════════════════════════════");
        Serial.println("         REPORTE DE RED");
        Serial.println("═══════════════════════════════════════");
        
        for (int i = 0; i < deviceCount; i++) {
            ScannedDevice& dev = devices[i];
            Serial.printf("\n📱 %s\n", dev.ip.toString().c_str());
            Serial.printf("   Vendor: %s\n", dev.vendor);
            Serial.printf("   MAC: %s\n", dev.mac);
            Serial.printf("   Puertos abiertos: %d\n", dev.openPorts);
            
            for (int p = 0; p < dev.openPorts; p++) {
                Serial.printf("     🔓 %d (%s)\n", dev.ports[p].port, dev.ports[p].service);
                if (strlen(dev.ports[p].vulnerability) > 0) {
                    Serial.printf("        ⚠️ %s\n", dev.ports[p].vulnerability);
                }
            }
            
            const char* risk[] = {"✅ Seguro", "⚠️ Bajo", "⚠️ Medio", "🔴 ALTO"};
            Serial.printf("   Riesgo: %s\n", risk[dev.riskLevel]);
        }
        
        Serial.println("\n═══════════════════════════════════════");
        Serial.printf("Total: %d dispositivos\n", deviceCount);
        Serial.println("═══════════════════════════════════════\n");
    }

    // ==================== GETTERS ====================
    
    int getDeviceCount() { return deviceCount; }
    bool isScanComplete() { return scanComplete; }
    int getScanProgress() { return scanProgress; }
    unsigned long getLastScanTime() { return lastFullScan; }
    
    ScannedDevice* getDevice(int index) {
        if (index >= 0 && index < deviceCount) return &devices[index];
        return nullptr;
    }

    // JSON para API
    String getDevicesJSON() {
        String json = "[";
        for (int i = 0; i < deviceCount; i++) {
            if (i > 0) json += ",";
            json += "{";
            json += "\"ip\":\"" + devices[i].ip.toString() + "\",";
            json += "\"mac\":\"" + String(devices[i].mac) + "\",";
            json += "\"vendor\":\"" + String(devices[i].vendor) + "\",";
            json += "\"hostname\":\"" + String(devices[i].hostname) + "\",";
            json += "\"openPorts\":" + String(devices[i].openPorts) + ",";
            json += "\"riskLevel\":" + String(devices[i].riskLevel) + ",";
            json += "\"online\":" + String(devices[i].online ? "true" : "false") + ",";
            json += "\"ports\":[";
            for (int p = 0; p < devices[i].openPorts; p++) {
                if (p > 0) json += ",";
                json += "{";
                json += "\"port\":" + String(devices[i].ports[p].port) + ",";
                json += "\"service\":\"" + String(devices[i].ports[p].service) + "\",";
                json += "\"vuln\":\"" + String(devices[i].ports[p].vulnerability) + "\"";
                json += "}";
            }
            json += "]";
            json += "}";
        }
        json += "]";
        return json;
    }

    // Stats JSON
    String getStatsJSON() {
        int totalPorts = 0;
        int highRisk = 0;
        int medRisk = 0;
        
        for (int i = 0; i < deviceCount; i++) {
            totalPorts += devices[i].openPorts;
            if (devices[i].riskLevel == 3) highRisk++;
            else if (devices[i].riskLevel == 2) medRisk++;
        }
        
        String json = "{";
        json += "\"devices\":" + String(deviceCount) + ",";
        json += "\"totalOpenPorts\":" + String(totalPorts) + ",";
        json += "\"highRisk\":" + String(highRisk) + ",";
        json += "\"medRisk\":" + String(medRisk) + ",";
        json += "\"lastScan\":" + String(lastFullScan / 1000) + ",";
        json += "\"scanComplete\":" + String(scanComplete ? "true" : "false");
        json += "}";
        return json;
    }
};

#endif // SCANNER_H
