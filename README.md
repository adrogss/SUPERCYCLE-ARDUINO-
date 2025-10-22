# SUPERCYCLE — Control de ciclo 27h para cultivo

Por: @adrogss  
Fecha: 2025-10-22

Resumen
-------
SUPERCYCLE es un controlador basado en Arduino para gestionar un ciclo de iluminación y ventilación de 27 horas (13 h ON + 14 h OFF) orientado a cultivo interior. Además controla calefacción y humidificación según lecturas de DHT22, gestiona alarmas por gas (MQ-2) con bocina y tiene varias protecciones (bloqueo de sodio por temperatura alta, bloqueo temporal por reinicio por watchdog, persistencia en EEPROM, pantalla LCD y registro por Serial).

Características clave
--------------------
- Ciclo principal: 27 h (13 h ON, 14 h OFF), que se desplaza 3 h cada día (por definición del módulo de 27 h).
- Secuencia de encendido: pre-on extractores 5 s → encender LED (relayLampara1) → 10 s después encender sodio (relayLampara2).
- Durante el periodo ON los extractores/intractores siempre activos.
- Durante OFF, los extractores se pueden activar por humedad: ON si H >= 60%, OFF si H <= 45% (control hysteresis).
- Protección de Sodio:
  - Bloqueo permanente hasta reinicio manual si la temperatura ambiente ≥ 38 °C.
  - Bloqueo temporal si hubo un reinicio por watchdog en los últimos 30 minutos.
  - Bloqueos persistidos en EEPROM para el bloqueo por temperatura.
- Gestión de DHT22 con detección de lecturas estancadas (reinicializa la lectura si detecta valores repetidos).
- Alarmas MQ-2 (umbral configurable) con bocina y botón de reset para silenciar.
- Registro de última causa de reinicio por watchdog con timestamp persistente en EEPROM.
- LCD I2C 16x2 para estado (temperatura, humedad, hora actual y próxima transición).
- Watchdog (8 s) para robustez; si ocurre reset por WDT se registra timestamp en EEPROM (si RTC disponible).

Hardware / Pines
----------------
- relayExtractor (extractor)     — pin 2
- relayIntractor (intractor)     — pin 3
- relayCalefactor (calefactor)   — pin 4
- relayHumificador (humidificador)— pin 5
- relayBocina (bocina/alarma)    — pin 6
- relayLampara1 (LED)            — pin 7
- relayLampara2 (Sodio)          — pin 8
- DHT22 data                     — pin 9
- pinReset (botón reset alarma)  — pin 10 (INPUT_PULLUP)
- MQ-2 analog                    — A0
- LCD I2C                       — dirección 0x3F (ajustable)

Nota: Los relays están manejados como activo-LOW en el código (LOW = ON, HIGH = OFF). Asegúrate del tipo de módulo de relay que usas.

Librerías usadas
----------------
- Wire.h
- DHT.h
- RTClib.h (RTC_DS1307)
- LiquidCrystal_I2C.h
- EEPROM.h
- AVR watchdog: <avr/wdt.h>, <avr/io.h>

Cómo funciona (lógica principal)
--------------------------------
1. Inicio: se inicializan sensores, RTC, LCD, pines y se comprueba si el último reinicio fue por watchdog. Se activa WDT a 8 s.
2. Lecturas periódicas: DHT22 cada 2 s (con tolerancias para detectar estancamiento). MQ-2 leída en cada loop para alarmas.
3. Control de ambiente:
   - Calefactor: ON si T < 18 °C, OFF si T > 21 °C.
   - Humidificador: ON si H < 40%, OFF si H > 45%.
4. Ciclo de 27 h:
   - Se calcula epoch % 27h para decidir si estamos en ON u OFF.
   - Si queda < 5 s para el próximo ON, se activa un "pre-on": extractores ON antes del encendido de lámparas.
   - Al entrar en ON:
     - Extractores/intractores ON inmediatamente.
     - Tras 5 s se enciende LED.
     - Tras 10 s adicionales se intenta encender Sodio salvo bloqueo por temperatura o por watchdog reciente.
   - Al entrar en OFF:
     - Se apagan lámparas y extractores por ciclo (salvo que el control por humedad haya activado extractores).
5. Control por humedad en OFF:
   - Si H >= 60% y estamos en OFF (y no estamos en pre-on próximo), se encienden extractores.
   - Si H <= 45% se apagan.
6. Protecciones de Sodio:
   - Si la temperatura medida supera 38 °C, el sodio se bloquea permanentemente hasta que se reinicie el Arduino manualmente (persistido en EEPROM).
   - Si hubo un reinicio por watchdog en los últimos 30 minutos, el sodio no se encenderá en la siguiente ventana ON (bloqueo temporal).
7. EEPROM:
   - Dirección 0: byte que marca si Sodio está bloqueado por temperatura.
   - Dirección 4: unsigned long con el epoch del último reset por watchdog (se escribe si se detecta).
8. Watchdog:
   - WDT de 8 s para reinicios automáticos ante bloqueos; si ocurrió, se detecta en el arranque y se persiste timestamp (si RTC válido).

Interfaz / Salidas
------------------
- Serial (115200): logs de eventos (ON/OFF, bloqueos, DHT errores, extractores por humedad, watchdog).
- LCD 16x2:
  - Línea 1: T:xx H:yy HH:MM
  - Línea 2: Indica ON->HH:MM o OFF->HH:MM y tiempo restante (ej. "ON->14:30 5h20").
- Bocina: parpadea cada 5 s si MQ-2 supera umbral; se puede silenciar con botón reset dedicado.

Parámetros configurables (en el código)
---------------------------------------
- duracionCiclo, duracionEncendido, duracionApagado
- preOnMs, tiempoDelayExtractores, tiempoDelayLED
- umbralTemperaturaAlta (sodio lock)
- umbralHumedadOn, umbralHumedadOff
- temperaturaMin, temperaturaApagarCalefactor
- humedadMin, humedadOff
- umbral MQ-2 (const int umbralMQ2 = 6000; — ajustar según la lectura del sensor y Vref)

Consideraciones / recomendaciones
---------------------------------
- Asegúrate de que la RTC (DS1307) esté con pila/backup y ajustada; si está mal, el código usa millis() como fallback para comportamiento del ciclo.
- Los relays están configurados como activos en LOW: verifica conexión al módulo de relays (VCC, GND, INx).
- Valida el umbral del sensor MQ-2 en tu hardware; la lectura analógica depende de la carga de la placa, Vcc y calibración del sensor.
- La persistencia de bloqueo de sodio evita encendidos tras temperaturas críticas incluso después de reinicios bruscos; para desbloquearlo hay que reiniciar manualmente el Arduino (o cambiar la EEPROM).
- Evitar usar delay() largos en loop para no interferir con lecturas y watchdog; el diseño usa millis().

Ejemplo de logs (Serial)
------------------------
- "SUPERCYCLE - Iniciando..."
- "EEPROM sodio blocked: 0"
- "Inicio ciclo ON"
- "LED ON"
- "Sodio ON" / "Sodio bloqueado por temperatura"
- "Extractores ON por humedad"
- "DHT estancado - reiniciando"
- "Watchdog reset detectado - timestamp guardado"

Subida / Compilación
--------------------
- Incluye las librerías mencionadas en el gestor de librerías de Arduino IDE.
- Selecciona la placa correspondiente (ej. Arduino Uno / Nano) y puerto serie en Arduino IDE.
- Compila y sube normalmente.
- Ajusta la dirección I2C del LCD si usas 0x27 u otra.

Ideas futuras / mejoras
-----------------------
- Interfaz web o MQTT para monitoreo remoto.
- Control PID para calefacción/kooling más fino.
- Históricos en tarjeta SD o envío por MQTT.
- Botón o comando para desbloquear Sodio desde UI o serial (actualmente requiere reinicio).

