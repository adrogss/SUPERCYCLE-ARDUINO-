/*
 * SUPERCYCLE - Control de ciclo 27h para cultivo
 * Por: @adrogss
 * Fecha: 2025-10-22
 * 
 * Ciclo: 27h (13h ON + 14h OFF)
 * - Ciclo se desfasa 3h cada día
 * - Pre-on extractores 5s
 * - Secuencia: extractores -> LED (5s) -> Sodio (10s)
 * - Durante ON: extractores siempre activos
 * - Durante OFF: extractores por humedad (ON>=60% / OFF<=45%)
 */

#include <Wire.h>
#include <DHT.h>
#include <RTClib.h>
#include <LiquidCrystal_I2C.h>
#include <avr/wdt.h>
#include <avr/io.h>
#include <EEPROM.h>

// PINES
const int relayExtractor = 2;
const int relayIntractor = 3; 
const int relayCalefactor = 4;
const int relayHumificador = 5;
const int relayLampara1 = 7; // LED
const int relayLampara2 = 8; // Sodio
const int relayBocina = 6;

const int pinMQ2 = A0;
const int pinReset = 10; // Botón reset alarma

// DHT
#define DHTPIN 9
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

// Constantes para DHT
const float DHT_TOLERANCIA_TEMP = 0.5; // ±0.5°C
const float DHT_TOLERANCIA_HUM = 1.0;  // ±1.0%

// RTC
RTC_DS1307 rtc;

// Estados de lámparas
bool lampara1On = false;
bool sodioOn = false;

// Parámetros de temperatura y humedad
const float temperaturaMin = 18.0;
const float temperaturaApagarCalefactor = 21.0;
const float humedadMin = 40.0;
const float humedadOff = 45.0;

// Sodio lock por temperatura (permanente hasta reinicio manual)
bool sodioTempBlocked = false;
const float umbralTemperaturaAlta = 38.0;

// Extractores por humedad cuando luces OFF: ON >=60, OFF <=45
const float umbralHumedadOn = 60.0;
const float umbralHumedadOff = 45.0;
bool extractoresEncendidosPorHumedad = false;

// Ciclo 27h -> usamos segundos para modulo
const unsigned long duracionCiclo = 27UL * 3600UL; // s
const unsigned long duracionEncendido = 13UL * 3600UL; // s
const unsigned long duracionApagado = 14UL * 3600UL; // s

// EEPROM addresses
const int ADDR_SODIO_BLOCKED = 0;        // 1 byte
const int ADDR_LAST_WDOG_RESET = 4;      // 4 bytes (unsigned long)

// Secuencia tiempos (ms)
const unsigned long preOnMs = 5000UL;       // 5s pre-on
const unsigned long tiempoDelayExtractores = 5000UL; // 5s desde extractores a LED
const unsigned long tiempoDelayLED = 10000UL;       // 10s desde LED a sodio

// LCD
LiquidCrystal_I2C lcd(0x3F,16,2);

// DHT timing
unsigned long lastDHTreadMs = 0;
const unsigned long intervaloDHT = 2000UL;
float ultimaTemperatura = NAN;
float ultimaHumedad = NAN;
int repetidasDHT = 0;

// Secuencia state
enum EstadoLamparas { ST_IDLE, ST_PREON, ST_EXTRACTORES_ON, ST_LED_ON, ST_SODIO_ON, ST_READY };
EstadoLamparas estadoLamparas = ST_IDLE;
unsigned long tExtractorOnMs = 0;
unsigned long tLedOnMs = 0;
bool lamparasEncendidas = false;

// Watchdog reset timestamp (epoch seconds) persisted
unsigned long lastWatchdogResetEpoch = 0; // 0 means none recorded

void setup() {
  Serial.begin(115200);
  Serial.println(F("SUPERCYCLE - Iniciando..."));
  
  dht.begin();
  Wire.begin();
  
  if (!rtc.begin()) {
    Serial.println(F("No se pudo encontrar RTC"));
  }
  
  if (!rtc.isrunning()) {
    Serial.println(F("RTC no está corriendo!"));
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }
  
  lcd.init();
  lcd.backlight();
  lcd.print(F("Iniciando..."));
  
  pinMode(relayExtractor, OUTPUT);
  pinMode(relayIntractor, OUTPUT);
  pinMode(relayCalefactor, OUTPUT);
  pinMode(relayHumificador, OUTPUT);
  pinMode(relayLampara1, OUTPUT);
  pinMode(relayLampara2, OUTPUT);
  pinMode(relayBocina, OUTPUT);
  pinMode(pinReset, INPUT_PULLUP);

  // Apagar relays (HIGH = OFF)
  digitalWrite(relayExtractor, HIGH);
  digitalWrite(relayIntractor, HIGH);
  digitalWrite(relayCalefactor, HIGH);
  digitalWrite(relayHumificador, HIGH);
  digitalWrite(relayLampara1, HIGH);
  digitalWrite(relayLampara2, HIGH);
  digitalWrite(relayBocina, HIGH);

  detectarWatchdogResetYPersistir();
  cargarEEPROM();

  Serial.println(F("Sistema iniciado - WDT: 8s"));
  wdt_enable(WDTO_8S);
}

void loop() {
  DateTime now = rtc.now();
  bool rtcValid = rtc.isrunning() && now.isValid();

  gestionarLecturaDHT();
  controlarCalefactor();
  controlarHumidificador();
  manejarAlarmasYReset();

  // Control temperatura sodio
  if(!isnan(ultimaTemperatura) && ultimaTemperatura >= umbralTemperaturaAlta) {
    if(!sodioTempBlocked) {
      sodioTempBlocked = true;
      guardarEEPROM_SodioBlockedIfChanged();
      digitalWrite(relayLampara2, HIGH);
      sodioOn = false;
      Serial.println(F("Sodio bloqueado por temperatura alta"));
    }
  }

  controlCentralExtractoresYLamparas(rtcValid, now);
  manejarExtractoresHumedad();

  // LCD update cada 1s
  static unsigned long lastLCD = 0;
  if(millis() - lastLCD >= 1000UL) {
    actualizarDisplay(rtcValid, now);
    lastLCD = millis();
  }

  guardarEEPROM_SodioBlockedIfChanged();
  wdt_reset();
}

void detectarWatchdogResetYPersistir() {
  uint8_t mcusr = MCUSR;
  bool wasWdog = mcusr & (1<<WDRF);
  MCUSR = 0;

  if(wasWdog) {
    DateTime now = rtc.now();
    if(now.isValid()) {
      unsigned long epoch = now.unixtime();
      EEPROM.put(ADDR_LAST_WDOG_RESET, epoch);
      Serial.println(F("Watchdog reset detectado - timestamp guardado"));
    } else {
      unsigned long marker = 0xFFFFFFFFUL;
      EEPROM.put(ADDR_LAST_WDOG_RESET, marker);
      Serial.println(F("Watchdog reset detectado - RTC inválido"));
    }
  }
}

void cargarEEPROM() {
  byte b = EEPROM.read(ADDR_SODIO_BLOCKED);
  sodioTempBlocked = (b != 0);

  unsigned long stored;
  EEPROM.get(ADDR_LAST_WDOG_RESET, stored);
  lastWatchdogResetEpoch = stored;
  if(lastWatchdogResetEpoch == 0xFFFFFFFFUL) {
    lastWatchdogResetEpoch = 0;
  }
  Serial.print(F("EEPROM sodio blocked: ")); Serial.println(sodioTempBlocked);
  Serial.print(F("EEPROM last wdog: ")); Serial.println(lastWatchdogResetEpoch);
}

void guardarEEPROM_SodioBlockedIfChanged() {
  static bool lastSaved = false;
  static bool first = true;
  if(first) {
    lastSaved = sodioTempBlocked;
    first = false;
  }
  if(sodioTempBlocked != lastSaved) {
    EEPROM.update(ADDR_SODIO_BLOCKED, sodioTempBlocked ? 1 : 0);
    lastSaved = sodioTempBlocked;
    Serial.println(F("EEPROM: sodioBlocked actualizado"));
  }
}

void gestionarLecturaDHT() {
  if(millis() - lastDHTreadMs < intervaloDHT) return;
  lastDHTreadMs = millis();

  float t = dht.readTemperature();
  float h = dht.readHumidity();

  if(isnan(t) || isnan(h)) {
    Serial.println(F("Error lectura DHT"));
    return;
  }

  bool mismaTemp = false;
  bool mismaHum = false;
  
  if(!isnan(ultimaTemperatura)) {
    mismaTemp = abs(t - ultimaTemperatura) <= DHT_TOLERANCIA_TEMP;
  }
  
  if(!isnan(ultimaHumedad)) {
    mismaHum = abs(h - ultimaHumedad) <= DHT_TOLERANCIA_HUM;
  }

  if(mismaTemp && mismaHum) {
    repetidasDHT++;
  } else {
    repetidasDHT = 0;
    ultimaTemperatura = t;
    ultimaHumedad = h;
  }

  if(repetidasDHT >= 10) {
    Serial.println(F("DHT estancado - reiniciando"));
    dht.begin();
    repetidasDHT = 0;
  }
}

void actualizarDisplay(bool rtcValid, DateTime now) {
  static int lastTemp = -999;
  static int lastHum = -999;
  static unsigned long lastMinutoMostrado = 0;
  
  // Convertir a enteros
  int tempInt = isnan(ultimaTemperatura) ? -999 : (int)round(ultimaTemperatura);
  int humInt = isnan(ultimaHumedad) ? -999 : (int)round(ultimaHumedad);
  
  unsigned long minutoActual = rtcValid ? now.unixtime()/60 : millis()/60000;
  
  // Primera línea: T H HORA
  if(tempInt != lastTemp) {
    lcd.setCursor(0,0);
    lcd.print(F("T:"));
    if(tempInt != -999) {
      char buf[3];
      sprintf(buf,"%2d", tempInt);
      lcd.print(buf);
    } else {
      lcd.print(F("--"));
    }
    lastTemp = tempInt;
  }
  
  if(humInt != lastHum) {
    lcd.setCursor(5,0);
    lcd.print(F("H:"));
    if(humInt != -999) {
      char buf[3];
      sprintf(buf,"%2d", humInt);
      lcd.print(buf);
    } else {
      lcd.print(F("--"));
    }
    lastHum = humInt;
  }
  
  // Actualizar hora y estado si cambió el minuto
  if(minutoActual != lastMinutoMostrado) {
    // Mostrar hora actual
    lcd.setCursor(10,0);
    if(rtcValid) {
      char buf[6];
      sprintf(buf,"%02d:%02d", now.hour(), now.minute());
      lcd.print(buf);
    } else {
      lcd.print(F("--:--"));
    }
    
    // Segunda línea: Estado y próxima transición
    unsigned long epoch = leerEpochNowOrFallback(rtcValid, now);
    unsigned long tiempoCiclo = epoch % duracionCiclo;
    bool estaEnOn = (tiempoCiclo < duracionEncendido);
    
    lcd.setCursor(0,1);
    lcd.print(F("                ")); // Limpiar línea
    lcd.setCursor(0,1);
    
    if(estaEnOn) {
      // Calcular próximo OFF
      unsigned long restante = duracionEncendido - tiempoCiclo;
      unsigned long epochOff = epoch + restante;
      DateTime offTime = DateTime(epochOff);
      
      lcd.print(F("ON->"));
      char buf[6];
      sprintf(buf,"%02d:%02d", offTime.hour(), offTime.minute());
      lcd.print(buf);
      
      // Mostrar horas y minutos restantes
      unsigned long h = restante/3600UL;
      unsigned long m = (restante%3600UL)/60UL;
      lcd.print(F(" "));
      lcd.print(h);
      lcd.print(F("h"));
      lcd.print(m);
    } else {
      // Calcular próximo ON
      unsigned long restante = duracionCiclo - tiempoCiclo;
      unsigned long epochOn = epoch + restante;
      DateTime onTime = DateTime(epochOn);
      
      lcd.print(F("OFF->"));
      char buf[6];
      sprintf(buf,"%02d:%02d", onTime.hour(), onTime.minute());
      lcd.print(buf);
      
      // Mostrar horas y minutos restantes
      unsigned long h = restante/3600UL;
      unsigned long m = (restante%3600UL)/60UL;
      lcd.print(F(" "));
      lcd.print(h);
      lcd.print(F("h"));
      lcd.print(m);
    }
    
    lastMinutoMostrado = minutoActual;
  }
}

void controlarCalefactor() {
  if(isnan(ultimaTemperatura)) return;
  if(ultimaTemperatura < temperaturaMin) {
    digitalWrite(relayCalefactor, LOW);
  }
  else if(ultimaTemperatura > temperaturaApagarCalefactor) {
    digitalWrite(relayCalefactor, HIGH);
  }
}

void controlarHumidificador() {
  if(isnan(ultimaHumedad)) return;
  if(ultimaHumedad < humedadMin) {
    digitalWrite(relayHumificador, LOW);
  }
  else if(ultimaHumedad > humedadOff) {
    digitalWrite(relayHumificador, HIGH);
  }
}

void manejarAlarmasYReset() {
  const int umbralMQ2 = 6000;
  int val = analogRead(pinMQ2);
  static unsigned long lastBuzzToggle = 0;
  static bool buzzOn = false;

  if(val > umbralMQ2) {
    unsigned long ahora = millis();
    if(ahora - lastBuzzToggle >= 5000UL) {
      buzzOn = !buzzOn;
      lastBuzzToggle = ahora;
      digitalWrite(relayBocina, buzzOn ? LOW : HIGH);
    }
  } else {
    digitalWrite(relayBocina, HIGH);
    buzzOn = false;
  }

  if(digitalRead(pinReset) == LOW) {
    digitalWrite(relayBocina, HIGH);
    Serial.println(F("Botón reset presionado - alarma silenciada"));
  }
}

void controlCentralExtractoresYLamparas(bool rtcValid, DateTime now) {
  unsigned long epoch = leerEpochNowOrFallback(rtcValid, now);
  unsigned long tiempoCiclo = epoch % duracionCiclo;
  bool shouldBeOn = (tiempoCiclo < duracionEncendido);

  unsigned long timeUntilNextOnMs = 0;
  if(!shouldBeOn) {
    unsigned long secsLeft = duracionCiclo - tiempoCiclo;
    unsigned long fracMs = millis() % 1000UL;
    timeUntilNextOnMs = secsLeft * 1000UL - fracMs;
  }

  bool wdogBlockActive = false;
  if(lastWatchdogResetEpoch != 0 && rtcValid) {
    long diff = (long)epoch - (long)lastWatchdogResetEpoch;
    if(diff < 0) diff = 0;
    if(diff < 30UL*60UL) wdogBlockActive = true;
  }

  static bool preOnActive = false;
  if(!shouldBeOn && timeUntilNextOnMs <= preOnMs && !preOnActive && !lamparasEncendidas) {
    preOnActive = true;
    estadoLamparas = ST_PREON;
    tExtractorOnMs = millis();
    digitalWrite(relayExtractor, LOW);
    digitalWrite(relayIntractor, LOW);
    Serial.println(F("Pre-on: extractores ON"));
  }

  if(shouldBeOn && !lamparasEncendidas) {
    lamparasEncendidas = true;
    if(!preOnActive) {
      digitalWrite(relayExtractor, LOW);
      digitalWrite(relayIntractor, LOW);
      tExtractorOnMs = millis();
    }
    estadoLamparas = ST_EXTRACTORES_ON;
    tLedOnMs = 0;
    Serial.println(F("Inicio ciclo ON"));
  }

  if(!shouldBeOn && lamparasEncendidas) {
    lamparasEncendidas = false;
    preOnActive = false;
    digitalWrite(relayLampara1, HIGH);
    digitalWrite(relayLampara2, HIGH);
    digitalWrite(relayExtractor, HIGH);
    digitalWrite(relayIntractor, HIGH);
    estadoLamparas = ST_IDLE;
    tExtractorOnMs = 0;
    tLedOnMs = 0;
    lampara1On = false;
    sodioOn = false;
    Serial.println(F("Inicio ciclo OFF"));
  }

  if(lamparasEncendidas) {
    unsigned long nowMs = millis();

    digitalWrite(relayExtractor, LOW);
    digitalWrite(relayIntractor, LOW);
    extractoresEncendidosPorHumedad = false;

    switch(estadoLamparas) {
      case ST_EXTRACTORES_ON:
        if(nowMs - tExtractorOnMs >= tiempoDelayExtractores) {
          digitalWrite(relayLampara1, LOW);
          lampara1On = true;
          tLedOnMs = nowMs;
          estadoLamparas = ST_LED_ON;
          Serial.println(F("LED ON"));
        }
        break;

      case ST_LED_ON:
        if(nowMs - tLedOnMs >= tiempoDelayLED) {
          bool allowSodio = true;
          if(sodioTempBlocked) allowSodio = false;
          if(wdogBlockActive) allowSodio = false;
          
          if(allowSodio) {
            digitalWrite(relayLampara2, LOW);
            sodioOn = true;
            Serial.println(F("Sodio ON"));
          } else {
            digitalWrite(relayLampara2, HIGH);
            sodioOn = false;
            if(sodioTempBlocked) 
              Serial.println(F("Sodio bloqueado por temperatura"));
            else if(wdogBlockActive)
              Serial.println(F("Sodio bloqueado por watchdog reciente"));
          }
          estadoLamparas = ST_SODIO_ON;
        }
        break;

      default:
        break;
    }
  } else {
    if(!preOnActive) {
      // Nada aquí - manejado por control de humedad
    } else {
      if(timeUntilNextOnMs > preOnMs) {
        preOnActive = false;
      }
    }
  }

  if(preOnActive && lamparasEncendidas) {
    preOnActive = false;
  }
}

void manejarExtractoresHumedad() {
  if(lamparasEncendidas) return;
  if(isnan(ultimaHumedad)) return;

  DateTime now = rtc.now();
  bool rtcValid = rtc.isrunning() && now.isValid();
  unsigned long epoch = leerEpochNowOrFallback(rtcValid, now);
  unsigned long tiempoCicloSec = epoch % duracionCiclo;
  unsigned long timeUntilNextOnSec = duracionCiclo - tiempoCicloSec;
  unsigned long timeUntilNextOnMs = timeUntilNextOnSec * 1000UL - (millis()%1000UL);
  
  if(timeUntilNextOnMs <= preOnMs) return;

  if(ultimaHumedad >= umbralHumedadOn && !extractoresEncendidosPorHumedad) {
    digitalWrite(relayExtractor, LOW);
    digitalWrite(relayIntractor, LOW);
    extractoresEncendidosPorHumedad = true;
    Serial.println(F("Extractores ON por humedad"));
  } 
  else if(ultimaHumedad <= umbralHumedadOff && extractoresEncendidosPorHumedad) {
    digitalWrite(relayExtractor, HIGH);
    digitalWrite(relayIntractor, HIGH);
    extractoresEncendidosPorHumedad = false;
    Serial.println(F("Extractores OFF por humedad"));
  }
}

unsigned long leerEpochNowOrFallback(bool &rtcValid, DateTime &now) {
  if(rtcValid && now.isValid()) return now.unixtime();
  return millis()/1000UL;
}