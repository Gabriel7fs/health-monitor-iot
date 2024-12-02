#include <Wire.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include "BluetoothSerial.h"
#include "MAX30100_PulseOximeter.h"

#define REPORTING_PERIOD_MS 1000
#define USE_SERIAL Serial

// Variável para armazenar o CPF do usuário
String pacienteID = "";

// Configuração do sensor MAX30100
PulseOximeter pox;
float BPM, SpO2;
uint32_t tsLastReport = 0;
uint32_t lastBeatTime = 0;

// Configurações Wi-Fi WebSocket e Bluetooth
const char* ssid = "NOME_DA_REDE";
const char* password = "SENHA";

unsigned long lastWifiCheck = 0;
const unsigned long wifiCheckInterval = 30000;

const char* ws_host = "thehealthmonitor.cloud";
const int ws_port = 80;
const char* ws_baseurl = "/ws";

WebSocketsClient webSocket;

String device_name = "ESP32-Monitor";
BluetoothSerial SerialBT;

// Função de callback para eventos WebSocket
void webSocketEvent(WStype_t type, uint8_t* payload, size_t length) {
  if (type == WStype_DISCONNECTED) {
    Serial.println(F("[WebSocket] Desconectado!"));
  } else if (type == WStype_CONNECTED) {
    Serial.println(F("[WebSocket] Conectado ao servidor!"));
  } else if (type == WStype_TEXT) {
    Serial.print(F("[WebSocket] Mensagem recebida: "));
    Serial.println((char*)payload);
  }
}

// Envio de mensagem STOMP
void sendMessage(String BPM, String SpO2) {
  // Adicionando o CPF (pacienteID) na mensagem enviada
  String messageData = "[\"SEND\\ndestination:/app/chat/" + pacienteID + "\\n\\n{\\\"heartbeat\\\":" + String(BPM) + ",\\\"oxygenQuantity\\\":" + String(SpO2) + ",\\\"cpf\\\":\\\"" + pacienteID + "\\\"}\\u0000\"]";
  webSocket.sendTXT(messageData);
}

// Configuração do WebSocket
void connectToWebSocket() {
  // Gera um URL SockJS-like, como "/api/chat/123/456789/websocket"
  String socketUrl = "/api";              // Adiciona o prefixo /api
  socketUrl += String(ws_baseurl) + "/";  // Adiciona o restante do caminho
  socketUrl += random(100, 999);          // Número aleatório para imitar o SockJS
  socketUrl += "/";
  socketUrl += random(100000, 999999);  // Outro número aleatório
  socketUrl += "/websocket";            // Final esperado por SockJS

  Serial.print(F("Conectando ao WebSocket: "));
  Serial.println(socketUrl);

  // Configura o WebSocket com a URL SockJS
  webSocket.begin(ws_host, ws_port, socketUrl.c_str());
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(5000);
}

// Conectar ao WiFi
void connectToWifi() {
  WiFi.begin(ssid, password);
  unsigned long startTime = millis();
  const unsigned long timeout = 10000;

  while (WiFi.status() != WL_CONNECTED && (millis() - startTime) < timeout) {
    delay(500);
    Serial.print(F("."));
  }
}

void checkWifiConnection() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("Wi-Fi desconectado. Tentando reconectar..."));
    WiFi.disconnect();
    WiFi.begin(ssid, password);

    unsigned long startTime = millis();
    const unsigned long timeout = 10000;

    while (WiFi.status() != WL_CONNECTED && (millis() - startTime) < timeout) {
      delay(500);
      Serial.print(F("."));
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println(F("\nReconectado ao Wi-Fi!"));
      connectToWebSocket();  // Reconecta ao WebSocket após reconectar ao Wi-Fi
    } else {
      Serial.println(F("\nFalha ao reconectar ao Wi-Fi."));
    }
  }
}

// Função para enviar via Bluetooth
void sendBluetoothData(String bpm, String spo2) {
  if (SerialBT.hasClient()) {
    String data = "{\"bpm\": \"" + bpm + "\", \"spo2\": \"" + spo2 + "\"}";
    SerialBT.println(data);
    Serial.println("Bluetooth Dados enviados: " + data);
  } else {
    Serial.println("Nenhum dispositivo conectado ao Bluetooth.");
  }
}

// Função chamada quando um batimento é detectado
void onBeatDetected() {
  uint32_t currentTime = millis();
  // Verifica se passaram pelo menos 300 ms desde a última detecção (para evitar falsos positivos)
  if (currentTime - lastBeatTime > 300 && BPM > 0 && BPM < 220) {
    lastBeatTime = currentTime;
  }
}

// Função para solicitar o CPF do usuário via Serial Monitor
void requestCPF() {
  Serial.println(F("Insira o CPF do paciente: "));
  while (pacienteID.isEmpty()) {
    if (Serial.available()) {
      pacienteID = Serial.readStringUntil('\n');
      pacienteID.trim();
    }
  }
  Serial.print(F("CPF armazenado: "));
  Serial.println(pacienteID);
}

void setup() {
  Serial.begin(115200);

  pinMode(22, OUTPUT);

  connectToWifi();

  // Verifica se o WiFi está conectado
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("\nFalha ao conectar ao WiFi."));
    // Inicializa o Bluetooth apenas se o WiFi não estiver conectado
    if (!SerialBT.begin(device_name)) {
      Serial.println(F("Erro ao inicializar o Bluetooth."));
      while (1)
        ;
    }
    Serial.println(F("Bluetooth inicializado."));
  } else {
    Serial.println(F("Conectado ao Wifi."));
    connectToWebSocket();
  }

  requestCPF();

  // Inicializa o sensor e verifica se a inicialização foi bem-sucedida
  if (!pox.begin()) {
    Serial.println(F("Falha ao inicializar o oxímetro!"));
    while (true)
      ;
  }
  Serial.println(F("Oxímetro inicializado com sucesso."));

  // Configura a função de callback para detecção de batidas
  pox.setOnBeatDetectedCallback(onBeatDetected);
}

void loop() {
  // Atualiza os dados do sensor
  pox.update();
  // Obtém a frequência cardíaca e o nível de SpO2
  BPM = pox.getHeartRate();
  SpO2 = pox.getSpO2();

  // Verifica se as leituras são válidas e estão dentro do intervalo aceitável antes de exibi-las
  if (BPM > 0 && BPM < 220 && SpO2 > 0) {
    // Exibe os dados no monitor serial a cada segundo
    if (millis() - tsLastReport > REPORTING_PERIOD_MS) {
      Serial.print(F("BPM: "));
      Serial.println(BPM, 1);
      Serial.print(F("SpO2: "));
      Serial.print(SpO2, 1);
      Serial.println(F(" %"));

      if (WiFi.status() == WL_CONNECTED) {

        sendMessage(String(BPM), String(SpO2));

      } else {
        sendBluetoothData(String(BPM), String(SpO2));
      }

      tsLastReport = millis();
    }
  }

  if (millis() - lastWifiCheck > wifiCheckInterval) {
    lastWifiCheck = millis();
    checkWifiConnection();
  }

  if (WiFi.status() == WL_CONNECTED) {
    // Atualiza o WebSocket
    webSocket.loop();
  }
}
