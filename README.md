# 🏠 ESP32 Hogar - Dashboard + Protector de Red

Sistema de monitoreo doméstico con panel web y protección de red.

## ✅ Componentes Confirmados

- [x] **1.8" TFT SPI Display (ST7735, 128x160)** - Display para mostrar datos
- [x] **Sensor PIR HC-SR501 5V** - Detección de movimiento

## 📋 Componentes Necesarios

### Hardware
- ESP32 (cualquier variante)
- **1.8" TFT SPI Display (ST7735, 128x160)** ✅
- Sensor DHT22 o DHT11 (temperatura/humedad)
- **Sensor PIR HC-SR501 5V** ✅
- Sensor magnético de puerta (reed switch)
- LED RGB (cathode común)
- Buzzer pasivo
- Resistencias 220Ω (para LEDs)
- Protoboard y cables

### Conexiones
```
=== TFT DISPLAY 1.8" ST7735 ===
  VCC  → 3.3V
  GND  → GND
  CS   → GPIO 22
  RESET→ GPIO 23
  DC   → GPIO 21
  MOSI → GPIO 23 (SPI)
  SCK  → GPIO 18 (SPI)
  LED  → 3.3V ( backlight)
  SDA  → No usar (MOSI)
  SCL  → No usar (SCK)

=== SENSORES ===
DHT22:
  VCC → 3.3V
  GND → GND
  DATA → GPIO 4

PIR HC-SR501 (5V):
  VCC → 5V ✅
  GND → GND
  OUT → GPIO 5
  Sensibilidad → Ajustar con potenciómetro
  Tiempo → Ajustar con potenciómetro

Sensor Puerta (reed switch):
  Un pin → GPIO 15
  Otro pin → GND
  (con resistencia pull-up interna)

LED RGB:
  R → GPIO 16 (con 220Ω)
  G → GPIO 17 (con 220Ω)
  B → GPIO 18 (con 220Ω)
  Common → GND

Buzzer:
  + → GPIO 19
  - → GND
```

### ⚠️ Nota sobre el PIR 5V
El PIR HC-SR501 funciona a 5V, pero sus **pines de salida son 3.3V** (compatible con ESP32). Conecta:
- VCC a 5V del ESP32
- GND a GND
- OUT a GPIO 5

Ajusta los potenciómetros del PIR:
- **Sensibilidad**: Cuánto movimiento detecta
- **Tiempo**: Cuánto tiempo mantiene el pin HIGH después de detectar

## 🔧 Instalación

### 1. PlatformIO (recomendado)
```bash
cd esp32-hogar
platformio run --target upload
```

### 2. Arduino IDE
1. Abrir `src/main.cpp`
2. Instalar librerías: DHT sensor library, ESPAsyncWebServer, AsyncTCP
3. Seleccionar placa: ESP32 Dev Module
4. Subir

### 3. Configurar WiFi
Editar en `src/main.cpp`:
```cpp
const char* ssid = "TU_RED_WIFI";
const char* password = "TU_PASSWORD";
```

## 🌐 Uso

1. Subir el código al ESP32
2. Abrir el Monitor Serial a 115200 baudios
3. Copiar la IP que aparece (ej: `192.168.1.100`)
4. Abrir esa IP en cualquier navegador del celular o PC
5. ¡Listo! Dashboard en tiempo real

## 🔒 Protector de Red

El sistema escanea la red periódicamente y:
- Detecta dispositivos nuevos conectados
- Alerta cuando hay dispositivos desconocidos
- Muestra lista de todos los dispositivos en la red
- Registra intentos de conexión sospechosos

## 📡 API Endpoints

El ESP32 expone estos endpoints HTTP:

| Método | Ruta | Descripción |
|--------|------|-------------|
| GET | `/` | Dashboard principal |
| GET | `/api/data` | Datos de sensores (JSON) |
| GET | `/api/network` | Dispositivos en la red |
| GET | `/api/alerts` | Alertas de seguridad |
| POST | `/api/alert/ack` | Confirmar alerta |
| GET | `/api/stats` | Estadísticas del sistema |
| GET | `/api/agent` | Estado del agente oculto |

## 🥷 Modo Agente Oculto

El ESP32 puede funcionar como un **agente de monitoreo oculto** de red:

### Activar modo stealth

En `src/network/guardian.h`:
```cpp
#define STEALTH_MODE true  // Modo oculto
```

### Características del modo stealth:
- **LED apagado**: No muestra actividad visual
- **Serial mínimo**: Solo errores críticos
- **Escaneo pasivo**: Monitorea sin ser detectado
- **Alertas silenciosas**: Registra eventos sin notificar

### Uso educativo

Ideal para demostraciones de:
- Detección de dispositivos no autorizados
- Monitoreo de red en tiempo real
- Análisis de tráfico de red
- Respuesta a incidentes

## 🛠️ Personalización

### Cambiar intervalo de escaneo
En `src/network/guardian.h`:
```cpp
#define SCAN_INTERVAL 30000  // 30 segundos
```

### Agregar dispositivos confiables
En `src/network/guardian.h`:
```cpp
const char* trusted_devices[] = {
  "AA:BB:CC:DD:EE:FF",  // Mi celular
  "11:22:33:44:55:66",  // Laptop
};
```

### Cambiar colores del LED
Los colores se ajustan automáticamente según el estado:
- 🟢 Verde: Todo normal
- 🟡 Amarillo: Dispositivo nuevo detectado
- 🔴 Rojo: Alerta de seguridad activa

### Ajustar el PIR
El HC-SR501 tiene 2 potenciómetros:
1. **Sensibilidad (izquierda)**: Gira para ajustar sensibilidad al movimiento
2. **Tiempo (derecha)**: Gira para ajustar cuánto tiempo se mantiene HIGH

### Display TFT
El display muestra:
- Temperatura y humedad en tiempo real
- Estado de movimiento y puerta
- Número de dispositivos en la red
- IP del ESP32
- Estado de conexión WiFi

## 🔧 Solución de Problemas

### Display TFT no muestra nada
1. Verificar conexiones CS, DC, RST
2. Probar `tft.initR(INRBLACKTAB)` (puede ser `INITR_144GREENTAB` o `INITR_MINI160x80`)
3. Verificar que SPI no esté siendo usado por otra cosa

### PIR detecta todo el tiempo
1. Reducir sensibilidad con el potenciómetro izquierdo
2. Aumentar tiempo con el potenciómetro derecho
3. Asegurar que no haya movimiento cerca (aire acondicionado, etc.)

### WiFi no conecta
1. Verificar SSID y password
2. Asegurar que la red sea 2.4GHz (ESP32 no soporta 5GHz)
3. Verificar que el router esté cerca

## 📝 Licencia

Proyecto educativo para el curso de ciberseguridad.
