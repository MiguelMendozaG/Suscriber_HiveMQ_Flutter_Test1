#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
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

// Macros
#define TIEMPO_CEBADO 3000
#define TIEMPO_REINTENTO 5000
#define FLUJO_MIN 5.0
#define INTERVALO_PUBLICACION 2000

WiFiClientSecure espClient;
PubSubClient client(espClient);

// =========================
// VARIABLES
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
// INTERRUPCIÓN
// =========================
void IRAM_ATTR contarPulsos() {
  pulsos++;
}

// =========================
// PUBLICAR ESTADO DE BOMBA
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

// =========================
// PUBLICAR MODO
// =========================
void publicarModo() {
  const char* modoStr = modoManual ? "MANUAL" : "AUTO";

  if (client.publish("esp32/modo", modoStr, true)) {
    Serial.print("Publicado modo: ");
    Serial.println(modoStr);
  } else {
    Serial.println("Error al publicar modo");
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
  mensaje.toUpperCase();

  String topicStr = String(topic);

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
    if (mensaje == "MANUAL") {
      modoManual = true;

      // Entrar limpio a modo manual
      digitalWrite(pinBomba, LOW);
      estadoBomba = false;
      estado = APAGADO;
      tiempoEstado = millis();

      publicarEstadoBomba();
      publicarModo();

      Serial.println("Modo cambiado a MANUAL");
    }
    else if (mensaje == "AUTO") {
      modoManual = false;

      // Regresar limpio a automático
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

    if (mensaje == "ON") {
      digitalWrite(pinBomba, HIGH);
      estadoBomba = true;
      estado = ENCENDIDO_OK;

      publicarEstadoBomba();

      Serial.println("Bomba ENCENDIDA manualmente");
    }
    else if (mensaje == "OFF") {
      digitalWrite(pinBomba, LOW);
      estadoBomba = false;
      estado = APAGADO;
      tiempoEstado = millis();

      publicarEstadoBomba();

      Serial.println("Bomba APAGADA manualmente");
    }
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

      Serial.println("Suscrito a esp32/datos");
      Serial.println("Suscrito a esp32/cmd/modo");
      Serial.println("Suscrito a esp32/cmd/bomba");

      publicarEstadoBomba();
      publicarModo();

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
      if (valor < 90 && (now - tiempoEstado >= TIEMPO_REINTENTO)) {
        Serial.println("Intentando encender bomba...");
        digitalWrite(pinBomba, HIGH);
        estadoBomba = true;
        estado = ESPERANDO_FLUJO;
        tiempoEstado = now;
      }
      break;

    case ESPERANDO_FLUJO:
      if (now - tiempoEstado >= TIEMPO_CEBADO) {
        if (flujo > FLUJO_MIN) {
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
      if (valor >= 90) {
        digitalWrite(pinBomba, LOW);
        estadoBomba = false;
        estado = APAGADO;
        tiempoEstado = now;
        Serial.println("Nivel OK, apagando");
      }
      else if (flujo < FLUJO_MIN) {
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