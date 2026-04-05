#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include "data.h"

// =========================
// CONFIGURACIÓN
// =========================
const char* ssid = ssid_data;
const char* password = password_data;

// HiveMQ Cloud
const char* mqtt_server = mqtt_server_data;
const int mqtt_port = mqtt_port_data;

const char* mqtt_user = mqtt_user_data;
const char* mqtt_pass = mqtt_pass_data;

// Pines
const int pinBomba = 2;
const int pinFlujo = 4;

// MQTT / NVS
WiFiClientSecure espClient;
PubSubClient client(espClient);
Preferences prefs;

// =========================
// VARIABLES CONFIGURABLES
// =========================
unsigned long tiempoCebado = 3000;
unsigned long tiempoReintento = 5000;

// Histéresis nivel
int nivelArranque = 85;
int nivelParo = 95;

// Histéresis flujo
float flujoOn = 5.5;   // Confirmar flujo válido
float flujoOff = 4.0;  // Confirmar pérdida de flujo

// Publicación de flujo
unsigned long intervaloFlujoCebado = 1000;
unsigned long intervaloFlujoActivo = 2000;

// =========================
// VARIABLES DE TRABAJO
// =========================
volatile unsigned long pulsos = 0;
float flujo = 0.0;

unsigned long ultimoCalculoFlujo = 0;
unsigned long ultimaPublicacionFlujo = 0;
unsigned long tiempoEstado = 0;

int valor = 100;

bool estadoBomba = false;
bool estadoBombaPrevio = false;
bool modoManual = false;
bool flujoCeroPublicado = false;

// Estado lógico de flujo con histéresis
bool flujoValido = false;

// Máquina de estados
enum Estado {
  APAGADO,
  ESPERANDO_FLUJO,
  ENCENDIDO_OK
};

Estado estado = APAGADO;

// =========================
// DECLARACIONES
// =========================
void publicarEstadoBomba();
void publicarModo();
void publicarConfiguracion();
void cargarConfiguracion();
void guardarConfiguracion();
void aplicarConfigPorTopico(const String& topicStr, const String& mensaje);
void publicarFlujo();
void publicarFlujoCero();
void actualizarEstadoFlujoValido();

// =========================
// INTERRUPCIÓN
// =========================
void IRAM_ATTR contarPulsos() {
  pulsos++;
}

// =========================
// PREFERENCES
// =========================
void cargarConfiguracion() {
  prefs.begin("config", true);

  tiempoCebado = prefs.getULong("cebado", 3000);
  tiempoReintento = prefs.getULong("reint", 5000);

  nivelArranque = prefs.getInt("nivarr", 85);
  nivelParo = prefs.getInt("nivparo", 95);

  flujoOn = prefs.getFloat("flon", 5.5);
  flujoOff = prefs.getFloat("floff", 4.0);

  intervaloFlujoCebado = prefs.getULong("ifceba", 1000);
  intervaloFlujoActivo = prefs.getULong("ifact", 2000);

  prefs.end();

  // Protección básica
  if (nivelArranque >= nivelParo) {
    nivelArranque = 85;
    nivelParo = 95;
  }

  if (flujoOff >= flujoOn) {
    flujoOn = 5.5;
    flujoOff = 4.0;
  }

  Serial.println("Configuración cargada desde NVS:");
  Serial.print("tiempoCebado = ");
  Serial.println(tiempoCebado);
  Serial.print("tiempoReintento = ");
  Serial.println(tiempoReintento);
  Serial.print("nivelArranque = ");
  Serial.println(nivelArranque);
  Serial.print("nivelParo = ");
  Serial.println(nivelParo);
  Serial.print("flujoOn = ");
  Serial.println(flujoOn);
  Serial.print("flujoOff = ");
  Serial.println(flujoOff);
  Serial.print("intervaloFlujoCebado = ");
  Serial.println(intervaloFlujoCebado);
  Serial.print("intervaloFlujoActivo = ");
  Serial.println(intervaloFlujoActivo);
}

void guardarConfiguracion() {
  prefs.begin("config", false);

  prefs.putULong("cebado", tiempoCebado);
  prefs.putULong("reint", tiempoReintento);

  prefs.putInt("nivarr", nivelArranque);
  prefs.putInt("nivparo", nivelParo);

  prefs.putFloat("flon", flujoOn);
  prefs.putFloat("floff", flujoOff);

  prefs.putULong("ifceba", intervaloFlujoCebado);
  prefs.putULong("ifact", intervaloFlujoActivo);

  prefs.end();

  Serial.println("Configuración guardada en NVS");
}

// =========================
// PUBLICACIONES MQTT
// =========================
void publicarEstadoBomba() {
  const char* estadoStr = estadoBomba ? "ON" : "OFF";

  if (client.publish("esp32/estado", estadoStr, true)) {
    Serial.print("Publicado estado bomba: ");
    Serial.println(estadoStr);
  } else {
    Serial.println("Error al publicar estado de bomba");
  }
}

void publicarModo() {
  const char* modoStr = modoManual ? "MANUAL" : "AUTO";

  if (client.publish("esp32/modo", modoStr, true)) {
    Serial.print("Publicado modo: ");
    Serial.println(modoStr);
  } else {
    Serial.println("Error al publicar modo");
  }
}

void publicarFlujo() {
  char flujoStr[16];
  dtostrf(flujo, 4, 2, flujoStr);

  if (client.publish("esp32/flujo", flujoStr, true)) {
    Serial.print("Publicado flujo: ");
    Serial.println(flujoStr);
  } else {
    Serial.println("Error al publicar flujo");
  }
}

void publicarFlujoCero() {
  if (client.publish("esp32/flujo", "0.00", true)) {
    Serial.println("Publicado flujo: 0.00");
  } else {
    Serial.println("Error al publicar flujo 0.00");
  }
}

void publicarConfiguracion() {
  char buffer[24];

  ultoa(tiempoCebado, buffer, 10);
  client.publish("esp32/config/estado/tiempo_cebado", buffer, true);

  ultoa(tiempoReintento, buffer, 10);
  client.publish("esp32/config/estado/tiempo_reintento", buffer, true);

  itoa(nivelArranque, buffer, 10);
  client.publish("esp32/config/estado/nivel_arranque", buffer, true);

  itoa(nivelParo, buffer, 10);
  client.publish("esp32/config/estado/nivel_paro", buffer, true);

  dtostrf(flujoOn, 4, 2, buffer);
  client.publish("esp32/config/estado/flujo_on", buffer, true);

  dtostrf(flujoOff, 4, 2, buffer);
  client.publish("esp32/config/estado/flujo_off", buffer, true);

  ultoa(intervaloFlujoCebado, buffer, 10);
  client.publish("esp32/config/estado/intervalo_flujo_cebado", buffer, true);

  ultoa(intervaloFlujoActivo, buffer, 10);
  client.publish("esp32/config/estado/intervalo_flujo_activo", buffer, true);

  Serial.println("Configuración publicada");
}

// =========================
// HISTÉRESIS FLUJO
// =========================
void actualizarEstadoFlujoValido() {
  if (!flujoValido) {
    if (flujo >= flujoOn) {
      flujoValido = true;
    }
  } else {
    if (flujo <= flujoOff) {
      flujoValido = false;
    }
  }
}

// =========================
// CONFIG POR TÓPICO
// =========================
void aplicarConfigPorTopico(const String& topicStr, const String& mensaje) {
  bool cambio = false;

  if (topicStr == "esp32/config/tiempo_cebado") {
    unsigned long nuevoValor = strtoul(mensaje.c_str(), nullptr, 10);
    if (nuevoValor >= 500 && nuevoValor <= 60000) {
      tiempoCebado = nuevoValor;
      cambio = true;
      Serial.print("Nuevo tiempoCebado: ");
      Serial.println(tiempoCebado);
    } else {
      Serial.println("Valor inválido para tiempo_cebado");
    }
  }
  else if (topicStr == "esp32/config/tiempo_reintento") {
    unsigned long nuevoValor = strtoul(mensaje.c_str(), nullptr, 10);
    if (nuevoValor >= 1000 && nuevoValor <= 3600000) {
      tiempoReintento = nuevoValor;
      cambio = true;
      Serial.print("Nuevo tiempoReintento: ");
      Serial.println(tiempoReintento);
    } else {
      Serial.println("Valor inválido para tiempo_reintento");
    }
  }
  else if (topicStr == "esp32/config/nivel_arranque") {
    int nuevoValor = mensaje.toInt();
    if (nuevoValor >= 1 && nuevoValor <= 99 && nuevoValor < nivelParo) {
      nivelArranque = nuevoValor;
      cambio = true;
      Serial.print("Nuevo nivelArranque: ");
      Serial.println(nivelArranque);
    } else {
      Serial.println("Valor inválido para nivel_arranque");
    }
  }
  else if (topicStr == "esp32/config/nivel_paro") {
    int nuevoValor = mensaje.toInt();
    if (nuevoValor >= 2 && nuevoValor <= 100 && nuevoValor > nivelArranque) {
      nivelParo = nuevoValor;
      cambio = true;
      Serial.print("Nuevo nivelParo: ");
      Serial.println(nivelParo);
    } else {
      Serial.println("Valor inválido para nivel_paro");
    }
  }
  else if (topicStr == "esp32/config/flujo_on") {
    float nuevoValor = mensaje.toFloat();
    if (nuevoValor >= 0.1 && nuevoValor <= 100.0 && nuevoValor > flujoOff) {
      flujoOn = nuevoValor;
      cambio = true;
      Serial.print("Nuevo flujoOn: ");
      Serial.println(flujoOn);
    } else {
      Serial.println("Valor inválido para flujo_on");
    }
  }
  else if (topicStr == "esp32/config/flujo_off") {
    float nuevoValor = mensaje.toFloat();
    if (nuevoValor >= 0.0 && nuevoValor < flujoOn) {
      flujoOff = nuevoValor;
      cambio = true;
      Serial.print("Nuevo flujoOff: ");
      Serial.println(flujoOff);
    } else {
      Serial.println("Valor inválido para flujo_off");
    }
  }
  else if (topicStr == "esp32/config/intervalo_flujo_cebado") {
    unsigned long nuevoValor = strtoul(mensaje.c_str(), nullptr, 10);
    if (nuevoValor >= 200 && nuevoValor <= 60000) {
      intervaloFlujoCebado = nuevoValor;
      cambio = true;
      Serial.print("Nuevo intervaloFlujoCebado: ");
      Serial.println(intervaloFlujoCebado);
    } else {
      Serial.println("Valor inválido para intervalo_flujo_cebado");
    }
  }
  else if (topicStr == "esp32/config/intervalo_flujo_activo") {
    unsigned long nuevoValor = strtoul(mensaje.c_str(), nullptr, 10);
    if (nuevoValor >= 200 && nuevoValor <= 60000) {
      intervaloFlujoActivo = nuevoValor;
      cambio = true;
      Serial.print("Nuevo intervaloFlujoActivo: ");
      Serial.println(intervaloFlujoActivo);
    } else {
      Serial.println("Valor inválido para intervalo_flujo_activo");
    }
  }

  if (cambio) {
    guardarConfiguracion();
    publicarConfiguracion();
  }
}

// =========================
// CALLBACK MQTT
// =========================
void callback(char* topic, byte* payload, unsigned int length) {
  String mensaje = "";

  for (unsigned int i = 0; i < length; i++) {
    mensaje += (char)payload[i];
  }

  mensaje.trim();

  String topicStr = String(topic);
  String mensajeUpper = mensaje;
  mensajeUpper.toUpperCase();

  Serial.print("Mensaje recibido [");
  Serial.print(topicStr);
  Serial.print("]: ");
  Serial.println(mensaje);

  if (topicStr == "esp32/datos") {
    valor = mensaje.toInt();
    Serial.print("Nivel actualizado: ");
    Serial.println(valor);
  }
  else if (topicStr == "esp32/cmd/modo") {
    if (mensajeUpper == "MANUAL") {
      modoManual = true;

      digitalWrite(pinBomba, LOW);
      estadoBomba = false;
      estado = APAGADO;
      tiempoEstado = millis();
      flujoValido = false;
      flujoCeroPublicado = false;

      publicarEstadoBomba();
      publicarModo();
      publicarFlujoCero();

      Serial.println("Modo cambiado a MANUAL");
    }
    else if (mensajeUpper == "AUTO") {
      modoManual = false;

      digitalWrite(pinBomba, LOW);
      estadoBomba = false;
      estado = APAGADO;
      tiempoEstado = millis();
      flujoValido = false;
      flujoCeroPublicado = false;

      publicarEstadoBomba();
      publicarModo();
      publicarFlujoCero();

      Serial.println("Modo cambiado a AUTO");
    }
  }
  else if (topicStr == "esp32/cmd/bomba") {
    if (!modoManual) {
      Serial.println("Comando de bomba ignorado: no está en modo MANUAL");
      return;
    }

    if (mensajeUpper == "ON") {
      digitalWrite(pinBomba, HIGH);
      estadoBomba = true;
      estado = ESPERANDO_FLUJO;
      tiempoEstado = millis();
      ultimaPublicacionFlujo = 0;
      flujoValido = false;
      flujoCeroPublicado = false;

      publicarEstadoBomba();

      Serial.println("Bomba ENCENDIDA manualmente");
    }
    else if (mensajeUpper == "OFF") {
      digitalWrite(pinBomba, LOW);
      estadoBomba = false;
      estado = APAGADO;
      tiempoEstado = millis();
      flujoValido = false;

      publicarEstadoBomba();
      publicarFlujoCero();
      flujoCeroPublicado = true;

      Serial.println("Bomba APAGADA manualmente");
    }
  }
  else if (
    topicStr == "esp32/config/tiempo_cebado" ||
    topicStr == "esp32/config/tiempo_reintento" ||
    topicStr == "esp32/config/nivel_arranque" ||
    topicStr == "esp32/config/nivel_paro" ||
    topicStr == "esp32/config/flujo_on" ||
    topicStr == "esp32/config/flujo_off" ||
    topicStr == "esp32/config/intervalo_flujo_cebado" ||
    topicStr == "esp32/config/intervalo_flujo_activo"
  ) {
    aplicarConfigPorTopico(topicStr, mensaje);
  }
}

// =========================
// WIFI
// =========================
void setup_wifi() {
  Serial.print("Conectando a WiFi...");
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi conectado");
  Serial.print("IP local: ");
  Serial.println(WiFi.localIP());
}

// =========================
// MQTT RECONNECT
// =========================
void reconnect() {
  while (!client.connected()) {
    Serial.print("Conectando a MQTT Cloud...");

    if (client.connect("ESP32_Controlador", mqtt_user, mqtt_pass)) {
      Serial.println(" conectado");

      client.subscribe("esp32/datos");
      client.subscribe("esp32/cmd/modo");
      client.subscribe("esp32/cmd/bomba");

      client.subscribe("esp32/config/tiempo_cebado");
      client.subscribe("esp32/config/tiempo_reintento");
      client.subscribe("esp32/config/nivel_arranque");
      client.subscribe("esp32/config/nivel_paro");
      client.subscribe("esp32/config/flujo_on");
      client.subscribe("esp32/config/flujo_off");
      client.subscribe("esp32/config/intervalo_flujo_cebado");
      client.subscribe("esp32/config/intervalo_flujo_activo");

      Serial.println("Suscrito a esp32/datos");
      Serial.println("Suscrito a esp32/cmd/modo");
      Serial.println("Suscrito a esp32/cmd/bomba");
      Serial.println("Suscrito a esp32/config/tiempo_cebado");
      Serial.println("Suscrito a esp32/config/tiempo_reintento");
      Serial.println("Suscrito a esp32/config/nivel_arranque");
      Serial.println("Suscrito a esp32/config/nivel_paro");
      Serial.println("Suscrito a esp32/config/flujo_on");
      Serial.println("Suscrito a esp32/config/flujo_off");
      Serial.println("Suscrito a esp32/config/intervalo_flujo_cebado");
      Serial.println("Suscrito a esp32/config/intervalo_flujo_activo");

      publicarEstadoBomba();
      publicarModo();
      publicarConfiguracion();

      if (!estadoBomba) {
        publicarFlujoCero();
      }

    } else {
      Serial.print(" fallo rc=");
      Serial.print(client.state());
      Serial.println(" reintentando en 5 segundos...");
      delay(5000);
    }
  }
}

// =========================
// SETUP
// =========================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("Arrancando ESP32 controlador...");

  pinMode(pinBomba, OUTPUT);
  digitalWrite(pinBomba, LOW);
  estadoBomba = false;
  estadoBombaPrevio = false;

  pinMode(pinFlujo, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(pinFlujo), contarPulsos, FALLING);

  cargarConfiguracion();
  setup_wifi();

  espClient.setInsecure();

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  tiempoEstado = millis();

  Serial.println("Setup terminado");
}

// =========================
// LOOP
// =========================
void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  unsigned long now = millis();

  // -------------------------
  // Calcular flujo cada 1 s
  // -------------------------
  if (now - ultimoCalculoFlujo >= 1000) {
    noInterrupts();
    unsigned long pulsosLocal = pulsos;
    pulsos = 0;
    interrupts();

    flujo = pulsosLocal / 7.5;

    Serial.print("Flujo medido: ");
    Serial.print(flujo);
    Serial.println(" L/min");

    actualizarEstadoFlujoValido();

    Serial.print("Flujo válido: ");
    Serial.println(flujoValido ? "SI" : "NO");

    ultimoCalculoFlujo = now;
  }

  // -------------------------
  // MODO MANUAL
  // -------------------------
  if (modoManual) {
    if (estadoBomba) {
      if (now - ultimaPublicacionFlujo >= intervaloFlujoCebado) {
        publicarFlujo();
        ultimaPublicacionFlujo = now;
      }

      flujoCeroPublicado = false;
    } else {
      if (!flujoCeroPublicado) {
        publicarFlujoCero();
        flujoCeroPublicado = true;
      }
    }

    if (estadoBomba != estadoBombaPrevio) {
      publicarEstadoBomba();
      estadoBombaPrevio = estadoBomba;
    }

    return;
  }

  // -------------------------
  // MÁQUINA DE ESTADOS AUTO
  // -------------------------
  switch (estado) {

    case APAGADO:
      if (valor < nivelArranque && (now - tiempoEstado >= tiempoReintento)) {
        Serial.println("Nivel bajo: intentando encender bomba...");
        digitalWrite(pinBomba, HIGH);
        estadoBomba = true;
        estado = ESPERANDO_FLUJO;
        tiempoEstado = now;
        ultimaPublicacionFlujo = 0;
        flujoValido = false;
        flujoCeroPublicado = false;
      }
      break;

    case ESPERANDO_FLUJO:
      if (now - tiempoEstado >= tiempoCebado) {
        if (flujoValido) {
          estado = ENCENDIDO_OK;
          Serial.println("Flujo confirmado, bomba estable");
        } else {
          digitalWrite(pinBomba, LOW);
          estadoBomba = false;
          estado = APAGADO;
          tiempoEstado = now;
          flujoValido = false;
          Serial.println("Sin flujo válido, apagando");
        }
      }
      break;

    case ENCENDIDO_OK:
      if (valor >= nivelParo) {
        digitalWrite(pinBomba, LOW);
        estadoBomba = false;
        estado = APAGADO;
        tiempoEstado = now;
        flujoValido = false;
        Serial.println("Nivel de paro alcanzado, apagando");
      }
      else if (!flujoValido) {
        digitalWrite(pinBomba, LOW);
        estadoBomba = false;
        estado = APAGADO;
        tiempoEstado = now;
        Serial.println("Flujo perdido, apagando bomba");
      }
      break;
  }

  // -------------------------
  // PUBLICACIÓN DE FLUJO
  // -------------------------
  if (estadoBomba) {
    unsigned long intervaloActual;

    if (estado == ESPERANDO_FLUJO) {
      intervaloActual = intervaloFlujoCebado;
    } else {
      intervaloActual = flujoValido ? intervaloFlujoActivo : intervaloFlujoCebado;
    }

    if (now - ultimaPublicacionFlujo >= intervaloActual) {
      publicarFlujo();
      ultimaPublicacionFlujo = now;
    }

    flujoCeroPublicado = false;
  } else {
    if (!flujoCeroPublicado) {
      publicarFlujoCero();
      flujoCeroPublicado = true;
    }
  }

  // -------------------------
  // PUBLICAR ESTADO SOLO SI CAMBIÓ
  // -------------------------
  if (estadoBomba != estadoBombaPrevio) {
    publicarEstadoBomba();
    estadoBombaPrevio = estadoBomba;
  }
}