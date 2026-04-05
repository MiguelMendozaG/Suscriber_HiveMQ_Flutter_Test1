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

// Constantes fijas
const unsigned long INTERVALO_PUBLICACION = 2000;

// MQTT / NVS
WiFiClientSecure espClient;
PubSubClient client(espClient);
Preferences prefs;

// =========================
// VARIABLES CONFIGURABLES
// =========================
unsigned long tiempoCebado = 3000;
unsigned long tiempoReintento = 5000;
float flujoMin = 5.0;
int nivelOn = 90;

// =========================
// VARIABLES DE TRABAJO
// =========================
volatile unsigned long pulsos = 0;
float flujo = 0.0;

unsigned long ultimoCalculoFlujo = 0;
unsigned long ultimaPublicacion = 0;
unsigned long tiempoEstado = 0;

int valor = 100;

bool estadoBomba = false;
bool estadoBombaPrevio = false;
bool modoManual = false;

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
  flujoMin = prefs.getFloat("flujomin", 5.0);
  nivelOn = prefs.getInt("nivelon", 90);

  prefs.end();

  Serial.println("Configuración cargada desde NVS:");
  Serial.print("tiempoCebado = ");
  Serial.println(tiempoCebado);
  Serial.print("tiempoReintento = ");
  Serial.println(tiempoReintento);
  Serial.print("flujoMin = ");
  Serial.println(flujoMin);
  Serial.print("nivelOn = ");
  Serial.println(nivelOn);
}

void guardarConfiguracion() {
  prefs.begin("config", false);

  prefs.putULong("cebado", tiempoCebado);
  prefs.putULong("reint", tiempoReintento);
  prefs.putFloat("flujomin", flujoMin);
  prefs.putInt("nivelon", nivelOn);

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

void publicarConfiguracion() {
  char buffer[24];

  ultoa(tiempoCebado, buffer, 10);
  client.publish("esp32/config/estado/tiempo_cebado", buffer, true);

  ultoa(tiempoReintento, buffer, 10);
  client.publish("esp32/config/estado/tiempo_reintento", buffer, true);

  dtostrf(flujoMin, 4, 2, buffer);
  client.publish("esp32/config/estado/flujo_min", buffer, true);

  itoa(nivelOn, buffer, 10);
  client.publish("esp32/config/estado/nivel_on", buffer, true);

  Serial.println("Configuración publicada");
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
  else if (topicStr == "esp32/config/flujo_min") {
    float nuevoValor = mensaje.toFloat();

    if (nuevoValor >= 0.1 && nuevoValor <= 100.0) {
      flujoMin = nuevoValor;
      cambio = true;
      Serial.print("Nuevo flujoMin: ");
      Serial.println(flujoMin);
    } else {
      Serial.println("Valor inválido para flujo_min");
    }
  }
  else if (topicStr == "esp32/config/nivel_on") {
    int nuevoValor = mensaje.toInt();

    if (nuevoValor >= 1 && nuevoValor <= 100) {
      nivelOn = nuevoValor;
      cambio = true;
      Serial.print("Nuevo nivelOn: ");
      Serial.println(nivelOn);
    } else {
      Serial.println("Valor inválido para nivel_on");
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

  // Nivel recibido desde el otro ESP32
  if (topicStr == "esp32/datos") {
    valor = mensaje.toInt();
    Serial.print("Nivel actualizado: ");
    Serial.println(valor);
  }

  // Cambio de modo desde la app
  else if (topicStr == "esp32/cmd/modo") {
    if (mensajeUpper == "MANUAL") {
      modoManual = true;

      digitalWrite(pinBomba, LOW);
      estadoBomba = false;
      estado = APAGADO;
      tiempoEstado = millis();

      publicarEstadoBomba();
      publicarModo();

      Serial.println("Modo cambiado a MANUAL");
    }
    else if (mensajeUpper == "AUTO") {
      modoManual = false;

      digitalWrite(pinBomba, LOW);
      estadoBomba = false;
      estado = APAGADO;
      tiempoEstado = millis();

      publicarEstadoBomba();
      publicarModo();

      Serial.println("Modo cambiado a AUTO");
    }
  }

  // Control manual de bomba desde la app
  else if (topicStr == "esp32/cmd/bomba") {
    if (!modoManual) {
      Serial.println("Comando de bomba ignorado: no está en modo MANUAL");
      return;
    }

    if (mensajeUpper == "ON") {
      digitalWrite(pinBomba, HIGH);
      estadoBomba = true;
      estado = ENCENDIDO_OK;

      publicarEstadoBomba();

      Serial.println("Bomba ENCENDIDA manualmente");
    }
    else if (mensajeUpper == "OFF") {
      digitalWrite(pinBomba, LOW);
      estadoBomba = false;
      estado = APAGADO;
      tiempoEstado = millis();

      publicarEstadoBomba();

      Serial.println("Bomba APAGADA manualmente");
    }
  }

  // Configuración remota
  else if (
    topicStr == "esp32/config/tiempo_cebado" ||
    topicStr == "esp32/config/tiempo_reintento" ||
    topicStr == "esp32/config/flujo_min" ||
    topicStr == "esp32/config/nivel_on"
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
      client.subscribe("esp32/config/flujo_min");
      client.subscribe("esp32/config/nivel_on");

      Serial.println("Suscrito a esp32/datos");
      Serial.println("Suscrito a esp32/cmd/modo");
      Serial.println("Suscrito a esp32/cmd/bomba");
      Serial.println("Suscrito a esp32/config/tiempo_cebado");
      Serial.println("Suscrito a esp32/config/tiempo_reintento");
      Serial.println("Suscrito a esp32/config/flujo_min");
      Serial.println("Suscrito a esp32/config/nivel_on");

      publicarEstadoBomba();
      publicarModo();
      publicarConfiguracion();

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

    Serial.print("Flujo: ");
    Serial.print(flujo);
    Serial.println(" L/min");

    ultimoCalculoFlujo = now;
  }

  // -------------------------
  // Publicar flujo
  // -------------------------
  if (now - ultimaPublicacion >= INTERVALO_PUBLICACION) {
    char flujoStr[16];
    dtostrf(flujo, 4, 2, flujoStr);

    if (client.publish("esp32/flujo", flujoStr, true)) {
      Serial.print("Publicado flujo: ");
      Serial.println(flujoStr);
    } else {
      Serial.println("Error al publicar flujo");
    }

    ultimaPublicacion = now;
  }

  // -------------------------
  // Si está en modo manual,
  // NO ejecutar lógica automática
  // -------------------------
  if (modoManual) {
    if (estadoBomba != estadoBombaPrevio) {
      publicarEstadoBomba();
      estadoBombaPrevio = estadoBomba;
    }
    return;
  }

  // -------------------------
  // Máquina de estados AUTO
  // -------------------------
  switch (estado) {

    case APAGADO:
      if (valor < nivelOn && (now - tiempoEstado >= tiempoReintento)) {
        Serial.println("Intentando encender bomba...");
        digitalWrite(pinBomba, HIGH);
        estadoBomba = true;
        estado = ESPERANDO_FLUJO;
        tiempoEstado = now;
      }
      break;

    case ESPERANDO_FLUJO:
      if (now - tiempoEstado >= tiempoCebado) {
        if (flujo > flujoMin) {
          estado = ENCENDIDO_OK;
          Serial.println("Flujo detectado, bomba estable");
        } else {
          digitalWrite(pinBomba, LOW);
          estadoBomba = false;
          estado = APAGADO;
          tiempoEstado = now;
          Serial.println("Sin flujo, apagando");
        }
      }
      break;

    case ENCENDIDO_OK:
      if (valor >= nivelOn) {
        digitalWrite(pinBomba, LOW);
        estadoBomba = false;
        estado = APAGADO;
        tiempoEstado = now;
        Serial.println("Nivel OK, apagando");
      }
      else if (flujo < flujoMin) {
        digitalWrite(pinBomba, LOW);
        estadoBomba = false;
        estado = APAGADO;
        tiempoEstado = now;
        Serial.println("Flujo perdido, apagando bomba");
      }
      break;
  }

  // -------------------------
  // Publicar estado solo si cambió
  // -------------------------
  if (estadoBomba != estadoBombaPrevio) {
    publicarEstadoBomba();
    estadoBombaPrevio = estadoBomba;
  }
}