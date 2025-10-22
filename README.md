# SUPERCYCLE — 

Por: @adrogss  
Fecha: 2025-10-22

Resumen
-------
SUPERCYCLE en controlador basado en Arduino para gestionar un ciclo de iluminación y ventilación de 27 horas (13 h ON + 14 h OFF). Controla además calefacción y humidificación con DHT22, alarmas con MQ-2, muestra estado en LCD I2C y registra información útil en EEPROM. Incluye protecciones para las luminarias (bloqueo de sodio por temperatura alta o por reinicio por watchdog reciente) y uso de watchdog para robustez.

Características principales
--------------------------
- Ciclo principal: 27 h (13 h ON + 14 h OFF) — se desplaza diariamente por la longitud del módulo.
- Secuencia de encendido:
  - Pre-on extractores 5 s antes del encendido.
  - Encender LED (relayLampara1) tras 5 s de extractores.
  - Encender Sodio (relayLampara2) 10 s después del LED.
- Durante ON: extractores/intractores siempre activos.
- Durante OFF: extractores pueden activarse por humedad (ON si H ≥ 60%, OFF si H ≤ 45%).
- Bloqueos del sodio:
  - Bloqueo permanente si temperatura ≥ 38 °C (persistido en EEPROM).
  - Bloqueo temporal si hubo reinicio por watchdog en los últimos 30 min.
- Lectura DHT22 con detección de estancamiento (reinicia DHT si lecturas repetidas).
- Alarma por MQ-2 (bocina) con botón de reset para silenciar.
- LCD 16x2 I2C para T/H, hora y próxima transición ON/OFF.
- Watchdog (8 s) y registro de última causa de reset por WDT (EEPROM).

Hardware / Pines
----------------
- relayExtractor (extractor)     — pin D2
- relayIntractor (intractor)     — pin D3
- relayCalefactor (calefactor)   — pin D4
- relayHumificador (humificador) — pin D5
- relayBocina (bocina/alarma)    — pin D6
- relayLampara1 (LED)            — pin D7
- relayLampara2 (Sodio)          — pin D8
- DHT22 data                     — pin D9
- pinReset (botón reset alarma)  — pin D10 (INPUT_PULLUP)
- MQ-2 analog                    — A0
- LCD I2C                        — dirección 0x3F (ajustable en caso necesario)
- RTC DS1307 (I2C)               — SDA/SCL (Wire)

Nota importante: los relays se manejan como activos en LOW (LOW = ON, HIGH = OFF). Verifica el módulo de relays que uses.

Parámetros configurables (en el código)
---------------------------------------
- duracionCiclo, duracionEncendido, duracionApagado
- preOnMs, tiempoDelayExtractores, tiempoDelayLED
- umbralTemperaturaAlta (bloqueo sodio)
- umbralHumedadOn (60%), umbralHumedadOff (45%)
- temperaturaMin (18°C), temperaturaApagarCalefactor (21°C)
- humedadMin (40%), humedadOff (45%)
- umbral MQ-2 (ej. 6000)

Comportamiento resumido (lógica)
--------------------------------
1. Al arrancar: inicializa DHT, RTC, LCD, pines; detecta si hubo reset por watchdog y guarda timestamp en EEPROM si RTC válido; activa WDT (8 s).
2. Lecturas: DHT cada 2 s (con tolerancias para detectar estancamiento). MQ-2 leída continuamente para alarma.
3. Control ambiental:
   - Calefactor ON si T < 18°C, OFF si T > 21°C.
   - Humidificador ON si H < 40%, OFF si H > 45%.
4. Ciclo 27 h:
   - epoch % 27h decide ON/OFF.
   - Si queda < preOnMs (5 s) para ON, activa extractores (pre-on).
   - Al entrar en ON: extractores ON, a los 5 s LED ON, a los 10 s sodio ON salvo bloqueos.
   - Al entrar en OFF: apaga lámparas y extractores por ciclo; extractores pueden reactivarse por humedad.
5. Extra: Si la temperatura ≥ 38°C se bloquea el sodio hasta reiniciar; si hubo WDT en último 30 min, sodio queda bloqueado temporalmente en la próxima ON.

Persistencia en EEPROM
----------------------
- Dirección 0: byte que indica si sodio está bloqueado por temperatura (0/1).
- Dirección 4: unsigned long con epoch del último reset por watchdog (si RTC válido).

Interfaz y logs
---------------
- Serial 115200: logs de eventos (ON/OFF, LED/Sodio, bloqueos, DHT, extractores por humedad, watchdog).
- LCD 16x2:
  - Línea 1: T:xx H:yy HH:MM
  - Línea 2: ON->HH:MM o OFF->HH:MM y tiempo restante.
- Bocina: suena intermitente si MQ-2 supera umbral; botón reset silencia.

Diagrama de conexión (ASCII)
----------------------------
Nota: simplificado. Asegúrate de alimentación 5V común y de la lógica activa del módulo de relays.

                     +5V
                      |
        +-------------+--------------+
        |                            |
      Arduino                      Módulos externos
    (Uno/Nano)
    +-----------+            +---------------------------+
    | D2  -> IN |------------| Relay IN Extractor        |
    | D3  -> IN |------------| Relay IN Intractor        |
    | D4  -> IN |------------| Relay IN Calefactor       |
    | D5  -> IN |------------| Relay IN Humificador      |
    | D6  -> IN |------------| Relay IN Bocina           |
    | D7  -> IN |------------| Relay IN LED (Lampara1)   |
    | D8  -> IN |------------| Relay IN Sodio (Lampara2) |
    | D9  -> DHT Data---------| DHT22 (VCC,GND,DATA)      |
    | A0  <- MQ-2 out         | MQ-2 (VCC,GND,AO)         |
    | D10 -> Botón Reset------| Botón (a GND, INPUT_PULLUP)|
    | SDA, SCL----------------| RTC DS1307 (I2C)          |
    | SDA, SCL----------------| LCD I2C 0x3F              |
    +-----------+            +---------------------------+

Ejemplo simplificado de conexiones:
- VCC de sensores y módulos a +5V
- GND comunes
- Si el módulo de relays es "activo LOW": conectar INx al pin Arduino; cuando Arduino escribe LOW, relé activa la carga.

Recomendaciones / consideraciones
--------------------------------
- Comprueba la dirección I2C del LCD (0x3F / 0x27).
- Calibra umbral MQ-2 según el sensor y el entorno.
- El bloqueo del sodio por temperatura queda persistente en EEPROM: para desbloquear actualmente hay que reiniciar el Arduino (o borrar la EEPROM / cambiar variable).
- RTC con batería de respaldo recomendado para mantener ciclo correcto.
- Evita delays largos en loop; el diseño usa millis() y watchdog.

Ejemplos de logs (Serial)
- "SUPERCYCLE - Iniciando..."
- "EEPROM sodio blocked: 0"
- "Inicio ciclo ON"
- "LED ON"
- "Sodio ON" / "Sodio bloqueado por temperatura"
- "Extractores ON por humedad"
- "DHT estancado - reiniciando"
- "Watchdog reset detectado - timestamp guardado"

Licencia
--------
Este repositorio incluye un archivo LICENSE (MIT) que acompaña a este README.

¿Quieres que además:
- agregue un diagrama PNG/SVG y lo incluya en el README (necesitarás subir la imagen al repo), o
- que haga el commit y push a una rama (dime owner/repo/branch)?

----
(La sección siguiente conserva exactamente la línea solicitada por el hilo del archivo README)
¿Quieres que genere también un archivo LICENSE (ej. MIT) y un ejemplo de diagrama de conexión en README (ASCII/imagen) para completar la documentación? Puedo añadirlo aquí mismo y preparar un README listo para subir.
