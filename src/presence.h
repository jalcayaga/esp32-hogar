/**
 * 🧑‍🤝‍满怀 Presence Detector PRO
 * 
 * Muestreo ultra rápido (100ms) + análisis de patrones
 * Extiende el RCWL-0516 al máximo de su capacidad.
 * 
 * Capacidades:
 * - Forma de onda en tiempo real
 * - Duración de cada evento
 * - Intensidad de movimiento (velocidad relativa)
 * - Distinguir "paso" vs "permanencia"
 * - Conteo inteligente de personas
 * - Correlación WiFi + Radar
 */

#ifndef PRESENCE_H
#define PRESENCE_H

#include <Arduino.h>
#include <WiFi.h>

#define SAMPLE_INTERVAL 100     // 100ms = 10 muestras/segundo
#define WAVEFORM_SIZE 120       // 12 segundos de forma de onda
#define MAX_HISTORY 120         // 2 minutos de eventos
#define PRESENCE_TIMEOUT 5000   // 5 segundos sin movimiento = ausente
#define MIN_EVENT_DURATION 300  // Mínimo 300ms para contar como evento
#define PASS_THRESHOLD 2000     // Menos de 2s = "paso"
#define STAY_THRESHOLD 5000     // Más de 5s = "permanencia"

// Evento de presencia
struct PresenceEvent {
    unsigned long timestamp;
    unsigned long duration;     // Duración del evento en ms
    uint8_t intensity;          // 0-100 (intensidad del movimiento)
    char type[10];              // "pass", "stay", "burst"
    int wifiDevices;
};

// Estado avanzado
struct AdvancedState {
    bool radarActive;
    unsigned long lastRadarOn;
    unsigned long lastRadarOff;
    unsigned long eventStart;
    uint16_t consecutiveHigh;
    uint16_t consecutiveLow;
    uint16_t totalEvents;
    uint16_t passCount;
    uint16_t stayCount;
    uint16_t burstCount;
    int estimatedPeople;
};

class PresenceDetector {
private:
    AdvancedState state;
    
    // Forma de onda en tiempo real
    uint8_t waveform[WAVEFORM_SIZE];
    int waveformIndex = 0;
    
    // Historial de eventos
    PresenceEvent history[MAX_HISTORY];
    int historyIndex = 0;
    int historyCount = 0;
    
    // Cálculos de intensidad
    uint16_t recentPulses;      // Pulsos en últimos 2 segundos
    unsigned long lastPulseCount;
    float intensity;            // 0.0 - 1.0
    
    // WiFi correlation
    int lastWifiDevices;
    unsigned long lastWifiUpdate;
    
    // Estimación de personas
    int personScore;
    int personEstimate;

public:
    void begin() {
        state.radarActive = false;
        state.lastRadarOn = 0;
        state.lastRadarOff = 0;
        state.eventStart = 0;
        state.consecutiveHigh = 0;
        state.consecutiveLow = 0;
        state.totalEvents = 0;
        state.passCount = 0;
        state.stayCount = 0;
        state.burstCount = 0;
        state.estimatedPeople = 0;
        
        recentPulses = 0;
        lastPulseCount = 0;
        intensity = 0;
        lastWifiDevices = 0;
        personScore = 0;
        personEstimate = 0;
        
        memset(waveform, 0, sizeof(waveform));
        Serial.println("🧑‍🤝‍满怀 Presence PRO iniciado (100ms sampling)");
    }

    // ==================== MUESTREO ULTRA RÁPIDO ====================
    
    // Llamar cada 100ms desde loop()
    void sample(bool radarRaw) {
        unsigned long now = millis();
        
        // Actualizar forma de onda
        waveform[waveformIndex] = radarRaw ? 255 : 0;
        waveformIndex = (waveformIndex + 1) % WAVEFORM_SIZE;
        
        // Contar pulsos recientes (últimos 2 segundos)
        if (radarRaw) {
            recentPulses++;
            state.consecutiveHigh++;
            state.consecutiveLow = 0;
        } else {
            state.consecutiveLow++;
            state.consecutiveHigh = 0;
        }
        
        // Calcular intensidad cada 2 segundos
        if (now - lastPulseCount >= 2000) {
            // Intensidad = pulsos por segundo (max 10 = 100%)
            intensity = min(1.0f, recentPulses / 20.0f);
            recentPulses = 0;
            lastPulseCount = now;
        }
        
        // Detectar transiciones
        if (radarRaw && !state.radarActive) {
            // Transición LOW → HIGH: inicio de evento
            state.radarActive = true;
            state.eventStart = now;
            state.lastRadarOn = now;
        }
        
        if (!radarRaw && state.radarActive) {
            // Transición HIGH → LOW: fin de evento
            state.radarActive = false;
            state.lastRadarOff = now;
            
            unsigned long duration = now - state.eventStart;
            
            // Solo contar si dura más de 300ms (ruido)
            if (duration >= MIN_EVENT_DURATION) {
                classifyEvent(duration);
            }
        }
        
        // Actualizar estimación de personas
        updatePersonEstimate();
    }

    // ==================== CLASIFICACIÓN DE EVENTOS ====================
    
    void classifyEvent(unsigned long duration) {
        PresenceEvent evt;
        evt.timestamp = millis();
        evt.duration = duration;
        evt.intensity = (uint8_t)(intensity * 100);
        evt.wifiDevices = lastWifiDevices;
        
        // Clasificar según duración
        if (duration < PASS_THRESHOLD) {
            // 0.3s - 2s: alguien pasó
            strcpy(evt.type, "pass");
            state.passCount++;
            personScore += 1;
        } else if (duration < STAY_THRESHOLD) {
            // 2s - 5s: alguien se detuvo brevemente
            strcpy(evt.type, "stay");
            state.stayCount++;
            personScore += 2;
        } else {
            // > 5s: alguien se quedó
            strcpy(evt.type, "burst");
            state.burstCount++;
            personScore += 3;
        }
        
        state.totalEvents++;
        
        // Agregar al historial
        history[historyIndex] = evt;
        historyIndex = (historyIndex + 1) % MAX_HISTORY;
        if (historyCount < MAX_HISTORY) historyCount++;
        
        // Debug
        Serial.printf("📡 Evento: %s (%lu ms, intensidad %d%%)\n", 
            evt.type, duration, evt.intensity);
    }

    // ==================== ESTIMACIÓN DE PERSONAS ====================
    
    void updatePersonEstimate() {
        unsigned long now = millis();
        
        // Decaimiento del score si no hay actividad
        if (now - state.lastRadarOn > 10000 && personScore > 0) {
            personScore = max(0, personScore - 1);
        }
        
        // Estimar personas basado en:
        // 1. Score acumulado de eventos
        // 2. WiFi devices
        // 3. Intensidad actual
        
        int fromRadar = 0;
        if (personScore > 10) fromRadar = 3;
        else if (personScore > 5) fromRadar = 2;
        else if (personScore > 0) fromRadar = 1;
        
        int fromWiFi = max(0, (lastWifiDevices - 1) / 2); // Router no cuenta
        
        // Tomar el mayor
        personEstimate = max(fromRadar, fromWiFi);
    }

    void updateWiFi(int devices) {
        lastWifiDevices = devices;
        lastWifiUpdate = millis();
    }

    // ==================== GETTERS ====================
    
    bool isPresent() { return state.radarActive || (millis() - state.lastRadarOn < PRESENCE_TIMEOUT); }
    float getIntensity() { return intensity; }
    uint8_t* getWaveform() { return waveform; }
    int getWaveformIndex() { return waveformIndex; }
    int getPersonEstimate() { return personEstimate; }
    int getPersonScore() { return personScore; }
    uint16_t getTotalEvents() { return state.totalEvents; }
    uint16_t getPassCount() { return state.passCount; }
    uint16_t getStayCount() { return state.stayCount; }
    uint16_t getBurstCount() { return state.burstCount; }
    int getWifiDevices() { return lastWifiDevices; }
    
    unsigned long getEventDuration() {
        if (!state.radarActive) return 0;
        return millis() - state.eventStart;
    }

    // ==================== JSON APIs ====================
    
    String getStateJSON() {
        String json = "{";
        json += "\"present\":" + String(isPresent() ? "true" : "false") + ",";
        json += "\"radarActive\":" + String(state.radarActive ? "true" : "false") + ",";
        json += "\"intensity\":" + String(intensity, 2) + ",";
        json += "\"eventDuration\":" + String(getEventDuration()) + ",";
        json += "\"people\":" + String(personEstimate) + ",";
        json += "\"score\":" + String(personScore) + ",";
        json += "\"totalEvents\":" + String(state.totalEvents) + ",";
        json += "\"passes\":" + String(state.passCount) + ",";
        json += "\"stays\":" + String(state.stayCount) + ",";
        json += "\"bursts\":" + String(state.burstCount) + ",";
        json += "\"wifiDevices\":" + String(lastWifiDevices);
        json += "}";
        return json;
    }

    String getWaveformJSON() {
        String json = "[";
        for (int i = 0; i < WAVEFORM_SIZE; i++) {
            int idx = (waveformIndex + i) % WAVEFORM_SIZE;
            if (i > 0) json += ",";
            json += String(waveform[idx]);
        }
        json += "]";
        return json;
    }

    String getHistoryJSON() {
        String json = "[";
        int start = (historyCount < 30) ? 0 : (historyIndex - 30 + MAX_HISTORY) % MAX_HISTORY;
        int count = min(historyCount, 30);
        for (int i = 0; i < count; i++) {
            int idx = (start + i) % MAX_HISTORY;
            if (i > 0) json += ",";
            json += "{";
            json += "\"t\":" + String(history[idx].timestamp / 1000) + ",";
            json += "\"d\":" + String(history[idx].duration) + ",";
            json += "\"i\":" + String(history[idx].intensity) + ",";
            json += "\"type\":\"" + String(history[idx].type) + "\",";
            json += "\"w\":" + String(history[idx].wifiDevices);
            json += "}";
        }
        json += "]";
        return json;
    }
};

#endif // PRESENCE_H
