#include <Arduino.h>
#include "lib/AutoOTA.h"

#include "Credentials.h"
#include <SPI.h>
#include "lib/LoRa/LoRa.h"
#include "lib/ESP32_MySQL/ESP32_MySQL.h"
#include <WiFiUdp.h>

#define ss 5
#define rst 14
#define dio0 2
#define ESP32_MYSQL_DEBUG_PORT Serial
#define _ESP32_MYSQL_LOGLEVEL_ 4

IPAddress server(91, 197, 98, 72);
uint16_t server_port = 3306;

char default_database[] = "test";
char default_table[]    = "logdata";
char get_my_ip_query[] = "SELECT HOST FROM `information_schema`.`processlist` WHERE `ID` = CONNECTION_ID()";
ESP32_MySQL_Connection conn((Client *)&client);

AutoOTA ota("4.6", "https://raw.githubusercontent.com/b33telgeuse/loggerStation/refs/heads/main/project.json");

struct txPack
{
  uint32_t device;
  uint32_t msg;
  float hum;
  float tempp;
  float pressr;
  float voltage;
} telem_packet;

ESP32_MySQL_Query sql_query = ESP32_MySQL_Query(&conn);

void setup()
{
  Serial.begin(115200);
  while (!Serial && millis() < 5000); // wait for serial port to connect
  Serial.print("Version ");
  Serial.println(ota.version());

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  int cntr = 0;
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.println(".");
    cntr++;
    if (cntr > 200)
    {
      ESP.restart();
    }
  }
  cntr = 0;

  Serial.print("Connected, local ip: ");
  Serial.println(WiFi.localIP());

  String ver, notes;
  bool should_update_fw = ota.checkUpdate(&ver, &notes);
  Serial.println(ver);
  Serial.println(notes);
  if (should_update_fw) {
    ota.updateNow();
  }

  ESP32_MYSQL_DISPLAY1("Connected to WiFi. My local IP-address is:", WiFi.localIP());
  ESP32_MYSQL_DISPLAY3("Connecting to SQL Server @", server, ", Port =", server_port);
  ESP32_MYSQL_DISPLAY5("User =", user, ", PW =", password, ", DB =", default_database);

  if (conn.connectNonBlocking(server, server_port, user, password) != RESULT_FAIL)
  {
    delay(500);
    //runQuery();
    conn.close();
  }
  else
  {
    ESP32_MYSQL_DISPLAY("\r\nConnect failed. Trying again on next iteration.");
  }

  Serial.println("LoRa initialization started");
  LoRa.setPins(ss, rst, dio0);

  while (!LoRa.begin(433E6)) {
    Serial.println(".");
    delay(500);
    cntr++;
    if (cntr > 20)
    {
      Serial.println("LoRa initialization failed!");
      if (conn.connectNonBlocking(server, server_port, user, password) != RESULT_FAIL)
      {
        delay(500);
        insertData(0, 0, "CURRENT_TIMESTAMP", 0, 0, 0, 0);
        conn.close();
      }
      else
      {
        ESP32_MYSQL_DISPLAY("\r\nConnect failed. Trying again on next iteration.");
      }
      ESP.restart();
    }
  }

  Serial.println("LoRa initialized!");

  LoRa.setSyncWord(0x12);
  LoRa.setSpreadingFactor(12);
  LoRa.setGain(6);
  LoRa.receive();
  LoRa.setCodingRate4(8);
  LoRa.enableCrc();
}

void insertData(uint32_t device_id, uint32_t msg_id, const char* time, float humidity, float temperature, float battery, int RSSII)
{
  uint32_t chipId = 0;
  for (int i = 0; i < 17; i = i + 8) {
    chipId |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
  }

  char query[512];
  sprintf(query, "INSERT INTO `test`.`logdata` (`device_id`, `msg_id`, `time`, `humidity`, `temperature`, `battery`, `rssi`, `station_id`, `chip_id`) VALUES ('%i', '%i', CURRENT_TIMESTAMP, '%f', '%f', '%f','%i', '%i', '%i')", device_id, msg_id, humidity, temperature, battery, RSSII, 32, chipId);

  ESP32_MySQL_Query query_mem = ESP32_MySQL_Query(&conn);
  if (!query_mem.execute(query))
  {
    ESP32_MYSQL_DISPLAY("Querying error");
    ESP.restart();
  }
}

char printBuf[512];
void loop()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    ESP.restart();
  }

  int packetSize = LoRa.parsePacket();

  if (packetSize) {
    Serial.print("\r\nReceived packet \"");

    byte bufferBytes[24];
    for (int k = 0; LoRa.available() && k < sizeof(bufferBytes); k++)
    {
      bufferBytes[k] = LoRa.read();
      sprintf(printBuf, "%02X", bufferBytes[k]);
      Serial.print(printBuf);
    }
    Serial.println("\"");
    memcpy(&telem_packet, &bufferBytes, 24);
    
    int packetRssi = LoRa.packetRssi();
    sprintf(printBuf, "device_id: %u\r\nmsg_id: %u\r\n\humidity: %f\r\ntemperature: %f\r\npressure: %f\r\nbattery: %f\r\nrssi: %i\r\n", telem_packet.device, telem_packet.msg, telem_packet.hum, telem_packet.tempp, telem_packet.pressr, telem_packet.voltage, packetRssi);
    Serial.println(printBuf);

    if (conn.connectNonBlocking(server, server_port, user, password) != RESULT_FAIL)
    {
      delay(500);
      insertData(telem_packet.device, telem_packet.msg, "CURRENT_TIMESTAMP", telem_packet.hum, telem_packet.tempp, telem_packet.voltage, packetRssi);
      conn.close();
    }
    else
    {
      ESP32_MYSQL_DISPLAY("\r\nConnect failed. Trying again on next iteration.");
      ESP.restart();
    }
  }

  delay(100);
  Serial.print(".");

  if (ota.tick()) {
    Serial.println("\r\nOTA: ");
    Serial.println((int)ota.getError());
  }
}
