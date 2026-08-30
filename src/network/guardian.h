/**
 * 🔒 Network Guardian - Protector de Red para ESP32
 * 
 * Escanea la red local, detecta dispositivos nuevos y genera alertas.
 * Modo agente oculto: monitoreo discreto de la red.
 * Para el curso de ciberseguridad.
 */

#ifndef NETWORK_GUARDIAN_H
#define NETWORK_GUARDIAN_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <ESP32Ping.h>

// ==================== CONFIGURACIÓN ====================

#define MAX_DEVICES 20           // Máximo de dispositivos a rastrear
#define MAX_ALERTS 10            // Máximo de alertas en historial
#define PING_TIMEOUT_MS 200      // Timeout de ping ICMP en ms

// Modo agente oculto (sin LED, mínimo output serial)
#define STEALTH_MODE false       // Cambiar a true para modo oculto

// Dispositivos conocidos (agregar los tuyos aquí)
const char* trusted_macs[] = {
    // "AA:BB:CC:DD:EE:FF",  // Ejemplo: Mi celular
    // "11:22:33:44:55:66",  // Ejemplo: Laptop
};

const char* trusted_names[] = {
    // "Mi Celular",
    // "Mi Laptop",
};

// ==================== ESTRUCTURAS ====================

struct Device {
    String mac;
    String ip;
    String name;
    bool trusted;
    bool isNew;
    unsigned long firstSeen;
    unsigned long lastSeen;
};

struct Alert {
    String message;
    String type;
    unsigned long timestamp;
};

// ==================== CLASE PRINCIPAL ====================

class NetworkGuardian {
private:
    Device devices[MAX_DEVICES];
    int deviceCount = 0;
    
    Alert alerts[MAX_ALERTS];
    int alertCount = 0;
    int alertIndex = 0;  // Para rotar alertas
    
    bool newDeviceDetected = false;
    unsigned long lastScanTime = 0;

    void log(const char* msg) {
        if (!STEALTH_MODE) {
            Serial.println(msg);
        }
    }

    void logf(const char* fmt, ...) {
        if (!STEALTH_MODE) {
            char buf[128];
            va_list args;
            va_start(args, fmt);
            vsnprintf(buf, sizeof(buf), fmt, args);
            va_end(args);
            Serial.print(buf);
        }
    }

public:
    void begin() {
        log("🔒 Network Guardian iniciado");
        deviceCount = 0;
        alertCount = 0;
    }

    // ==================== ESCANEO DE RED ====================

    // Escanear la red completa
    void scanNetwork() {
        log("🔍 Escaneando red...");
        
        // 1. Info de nuestra red
        IPAddress localIP = WiFi.localIP();
        IPAddress gw = WiFi.gatewayIP();
        IPAddress subnet = WiFi.subnetMask();
        
        logf("🌐 Red: %s / %s (GW: %s)\n",
            localIP.toString().c_str(),
            subnet.toString().c_str(),
            gw.toString().c_str());
        
        // 2. Ping sweep para descubrir hosts activos
        int found = pingSweep(localIP, subnet, gw);
        
        // 3. Actualizar timestamps
        lastScanTime = millis();
        
        logf("✅ Escaneo completado: %d hosts activos, %d dispositivos conocidos\n",
            found, deviceCount);
    }

    // Ping sweep ICMP de la subred local
    int pingSweep(IPAddress localIP, IPAddress subnet, IPAddress gateway) {
        // Calcular rango de la subred
        uint32_t ip = (uint32_t)localIP;
        uint32_t mask = (uint32_t)subnet;
        uint32_t network = ip & mask;
        uint32_t broadcast = network | ~mask;
        int found = 0;
        
        logf("🏓 Ping sweep: %s - %s\n",
            IPAddress(network + 1).toString().c_str(),
            IPAddress(broadcast - 1).toString().c_str());
        
        for (uint32_t i = network + 1; i < broadcast; i++) {
            IPAddress target(i);
            
            // Saltar nuestra propia IP
            if (target == localIP) continue;
            
            // Saltar broadcast
            if (target == IPAddress(broadcast)) continue;
            
            // Saltar IPs no privadas
            String targetStr = target.toString();
            if (!isPrivateIP(targetStr)) continue;
            
            // Ping ICMP real (1 paquete)
            if (Ping.ping(target, 1)) {
                found++;
                // Registrar como dispositivo activo
                String ipStr = target.toString();
                String macPlaceholder = "xx:xx:xx:xx:" + 
                    String((target[2] >> 4) & 0xF, HEX) + 
                    String(target[2] & 0xF, HEX) + ":" +
                    String((target[3] >> 4) & 0xF, HEX) + 
                    String(target[3] & 0xF, HEX);
                
                addOrUpdateDevice(ipStr, macPlaceholder);
                
                logf("  ✅ %s\n", ipStr.c_str());
            }
        }
        
        // Siempre agregar el gateway como conocido
        String gwStr = gateway.toString();
        String gwMac = "gw:default";
        addOrUpdateDevice(gwStr, gwMac);
        
        return found;
    }

    // ==================== GESTIÓN DE DISPOSITIVOS ====================

    // Verificar si una IP es privada (RFC 1918)
    bool isPrivateIP(String ip) {
        // Parsear la IP
        int firstOctet = ip.substring(0, ip.indexOf('.')).toInt();
        int secondOctet = ip.substring(ip.indexOf('.') + 1, ip.indexOf('.', ip.indexOf('.') + 1)).toInt();
        
        // 10.0.0.0/8
        if (firstOctet == 10) return true;
        // 172.16.0.0/12
        if (firstOctet == 172 && secondOctet >= 16 && secondOctet <= 31) return true;
        // 192.168.0.0/16
        if (firstOctet == 192 && secondOctet == 168) return true;
        // 127.0.0.0/8 (localhost)
        if (firstOctet == 127) return true;
        
        return false;
    }

    // Agregar o actualizar un dispositivo
    void addOrUpdateDevice(String ip, String mac) {
        // FILTRO: Solo IPs privadas/locales
        if (!isPrivateIP(ip)) {
            return;  // Ignorar IPs públicas
        }
        
        // Buscar por IP primero
        for (int i = 0; i < deviceCount; i++) {
            if (devices[i].ip == ip) {
                devices[i].lastSeen = millis();
                if (devices[i].mac != mac && mac != "gw:default") {
                    devices[i].mac = mac;
                }
                return;
            }
        }

        // Si no existe y hay espacio, agregarlo
        if (deviceCount < MAX_DEVICES) {
            Device& dev = devices[deviceCount];
            dev.mac = mac;
            dev.ip = ip;
            dev.name = getDeviceName(mac);
            dev.trusted = isTrusted(mac);
            dev.isNew = !dev.trusted;
            dev.firstSeen = millis();
            dev.lastSeen = millis();
            
            deviceCount++;
            
            // Generar alerta si es nuevo y no confiable
            if (dev.isNew && mac != "gw:default") {
                addAlert("Nuevo dispositivo: " + ip, "new_device");
                newDeviceDetected = true;
                logf("⚠️ Nuevo dispositivo detectado: %s\n", ip.c_str());
            }
        }
    }

    // Verificar si una MAC es confiable
    bool isTrusted(String mac) {
        for (unsigned int i = 0; i < sizeof(trusted_macs) / sizeof(trusted_macs[0]); i++) {
            if (mac.equalsIgnoreCase(trusted_macs[i])) {
                return true;
            }
        }
        return false;
    }

    // Obtener nombre del dispositivo por MAC
    String getDeviceName(String mac) {
        for (unsigned int i = 0; i < sizeof(trusted_macs) / sizeof(trusted_macs[0]); i++) {
            if (mac.equalsIgnoreCase(trusted_macs[i])) {
                return trusted_names[i];
            }
        }
        return "Desconocido";
    }

    // ==================== ALERTAS ====================

    // Agregar alerta
    void addAlert(String message, String type) {
        Alert& alert = alerts[alertIndex];
        alert.message = message;
        alert.type = type;
        alert.timestamp = millis();
        
        alertIndex = (alertIndex + 1) % MAX_ALERTS;
        if (alertCount < MAX_ALERTS) {
            alertCount++;
        }
        
        logf("🚨 Alerta: %s\n", message.c_str());
    }

    // Verificar si hay dispositivo nuevo
    bool hasNewDevice() {
        if (newDeviceDetected) {
            newDeviceDetected = false;
            return true;
        }
        return false;
    }

    // ==================== GETTERS ====================

    int getDeviceCount() {
        return deviceCount;
    }

    // Obtener dispositivos en JSON
    String getDevicesJSON() {
        JsonDocument doc;
        JsonArray devicesArray = doc["devices"].to<JsonArray>();
        
        for (int i = 0; i < deviceCount; i++) {
            JsonObject devObj = devicesArray.add<JsonObject>();
            devObj["mac"] = devices[i].mac;
            devObj["ip"] = devices[i].ip;
            devObj["name"] = devices[i].name;
            devObj["trusted"] = devices[i].trusted;
            devObj["new"] = devices[i].isNew;
            devObj["firstSeen"] = devices[i].firstSeen;
            devObj["lastSeen"] = devices[i].lastSeen;
        }
        
        doc["newDevices"] = countNewDevices();
        doc["lastScan"] = lastScanTime;
        doc["localIP"] = WiFi.localIP().toString();
        doc["gateway"] = WiFi.gatewayIP().toString();
        doc["subnet"] = WiFi.subnetMask().toString();
        doc["wifiRSSI"] = WiFi.RSSI();
        
        String response;
        serializeJson(doc, response);
        return response;
    }

    // Obtener alertas en JSON
    String getAlertsJSON() {
        JsonDocument doc;
        JsonArray alertsArray = doc["alerts"].to<JsonArray>();
        
        // Mostrar alertas de más reciente a más antigua
        int idx = (alertIndex - 1 + MAX_ALERTS) % MAX_ALERTS;
        for (int i = 0; i < alertCount; i++) {
            JsonObject alertObj = alertsArray.add<JsonObject>();
            alertObj["message"] = alerts[idx].message;
            alertObj["type"] = alerts[idx].type;
            alertObj["timestamp"] = alerts[idx].timestamp;
            
            idx = (idx - 1 + MAX_ALERTS) % MAX_ALERTS;
        }
        
        doc["total"] = alertCount;
        doc["stealthMode"] = STEALTH_MODE;
        
        String response;
        serializeJson(doc, response);
        return response;
    }

    // ========== MODO AGENTE OCULTO ==========
    
    bool isStealthMode() {
        return STEALTH_MODE;
    }
    
    // Obtener estado del agente
    String getAgentStatus() {
        JsonDocument doc;
        doc["mode"] = STEALTH_MODE ? "stealth" : "normal";
        doc["uptime"] = millis() / 1000;
        doc["devicesMonitored"] = deviceCount;
        doc["alertsGenerated"] = alertCount;
        doc["lastScan"] = lastScanTime;
        doc["freeHeap"] = ESP.getFreeHeap();
        
        String response;
        serializeJson(doc, response);
        return response;
    }

    int countNewDevices() {
        int count = 0;
        for (int i = 0; i < deviceCount; i++) {
            if (devices[i].isNew) {
                count++;
            }
        }
        return count;
    }
    
    // ========== UTILIDADES DE RED ==========
    
    String getMACByIP(String ip) {
        for (int i = 0; i < deviceCount; i++) {
            if (devices[i].ip == ip) {
                return devices[i].mac;
            }
        }
        return "";
    }
    
    bool isGateway(String ip) {
        return ip == WiFi.gatewayIP().toString();
    }
    
    bool isLocalIP(String ip) {
        return ip == WiFi.localIP().toString();
    }
};

#endif // NETWORK_GUARDIAN_H
