/*
   ESP32_MySQL - An optimized library for ESP32 to directly connect and execute SQL to MySQL database without intermediary.

   Copyright (c) 2024 Syafiqlim

   This software is released under the MIT License.
   https://opensource.org/licenses/MIT
*/
#include <Arduino.h>
#include <AutoOTA.h>
/*********************************************************************************************************************************
  SELECTquery_ESP32MySQL.ino
  by Syafiqlim @ syafiqlimx

 **********************************************************************************************************************************/
/*
  INSTRUCTIONS FOR USE

  1) Change the address of the server to the IP address of the MySQL server in Credentials.h
  2) Change the user and password to a valid MySQL user and password in Credentials.h
  3) Change the SSID and pass to match your WiFi network in Credentials.h
  4) Change the default DB, table, columns and value according to your DB schema and also the query
  5) Connect a USB cable to your ESP32
  6) Select the correct board and port
  7) Compile and upload the sketch to your ESP32
  8) Once uploaded, open Serial Monitor (use 115200 speed) and observet

*/
#include <NTPClient.h>
#include "Credentials.h"
#include <SPI.h>
#include <LoRa.h>
#include <ESP32_MySQL.h>
#include <WiFiUdp.h>
WiFiUDP ntpUDP; //Объект ntp
//NTPClient timeClient(ntpUDP, "europe.pool.ntp.org", 3600, 60000); //Так-же можно более детально настроить пул и задержку передачи.
#define ss 5
#define rst 14 
#define dio0 2
#define ESP32_MYSQL_DEBUG_PORT      Serial
// Debug Level from 0 to 4
#define _ESP32_MYSQL_LOGLEVEL_      4
#define USING_HOST_NAME     false
IPAddress server(91, 197, 98, 72);
uint16_t server_port = 3306;

char default_database[] = "logger";
char default_table[]    = "logdata";
String qquery = String("SELECT * FROM logger.logdata");

ESP32_MySQL_Connection conn((Client *)&client);
AutoOTA ota("3.9", "https://raw.githubusercontent.com/b33telgeuse/loggerStation/refs/heads/main/project.json");
struct txPack
{   
  uint32_t device;
  uint32_t msg;
  float hum;
  float tempp;
  float pressr;
  float voltage;
} telem_packet;
float bat=4.1;
ESP32_MySQL_Query sql_query = ESP32_MySQL_Query(&conn);

void setup()
{
  Serial.begin(115200);
  while (!Serial && millis() < 5000); // wait for serial port to connect

 // ESP32_MYSQL_DISPLAY1("\nStarting Basic_Insert_ESP on", ARDUINO_BOARD);

//   Begin WiFi section
 ESP32_MYSQL_DISPLAY1("Connecting to", ssid);

  WiFi.begin(ssid, pass);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    ESP32_MYSQL_DISPLAY0(".");
  }
    Serial.println("Connected");
    Serial.println(WiFi.localIP());
    Serial.print("Version ");
    Serial.println(ota.version());
      String ver, notes;
    if (ota.checkUpdate(&ver, &notes)) {
        Serial.println(ver);
        Serial.println(notes);
        ota.updateNow();
    }

  // print out info about the connection:
 ESP32_MYSQL_DISPLAY1("Connected to network. My IP address is:", WiFi.localIP());

  ESP32_MYSQL_DISPLAY3("Connecting to SQL Server @", server, ", Port =", server_port);
  ESP32_MYSQL_DISPLAY5("User =", user, ", PW =", password, ", DB =", default_database);
  LoRa.setPins(ss, rst, dio0);
  //замените аргумент LoRa.begin(---E-) частотой сигнала.
  while (!LoRa.begin(433E6)) {
    Serial.println(".");
    delay(500);
  }
  LoRa.setSyncWord(0x12);
  LoRa.setSpreadingFactor(12);
  LoRa.setGain(6);
  LoRa.receive();
  LoRa.setCodingRate4(8);
  LoRa.enableCrc();
  Serial.print("Version ");
  Serial.println(ota.version());
}
void insertData( uint32_t device_id, uint32_t msg_id, const char* time, float humidity, float temperature, float battery, int RSSII) {
 /* float prikol = random(100);
  prikol = prikol/100000.0;
  Serial.println(prikol,5);
  
  bat=bat-abs(prikol);
  temperature=temperature+prikol*500.0;
  humidity=humidity+prikol*1000.0;
  */
  char query[256];
  sprintf(query, "INSERT INTO logger.logdata (device_id, msg_id, time, humidity, temperature, battery) VALUES ('%i', '%i', CURRENT_TIMESTAMP, '%f', '%f', '%f')", device_id, msg_id, humidity, temperature, battery);
  //String q(query);
  ESP32_MySQL_Query query_mem = ESP32_MySQL_Query(&conn);
  if ( !query_mem.execute(query) )
  {
    ESP32_MYSQL_DISPLAY("Querying error");
    return;
  }
  sprintf(query, "INSERT INTO test.logdata (device_id, msg_id, time, humidity, temperature, battery, rssi, station_id) VALUES ('%i', '%i', CURRENT_TIMESTAMP, '%f', '%f', '%f','%i','%i')", device_id, msg_id, humidity, temperature, battery, RSSII, 10);
   // String g(query);
  ESP32_MySQL_Query query_memm = ESP32_MySQL_Query(&conn);
  if ( !query_memm.execute(query) )
  {
    ESP32_MYSQL_DISPLAY("Querying error");
    return;
  }
}
/*void runQuery()
{

//  ESP32_MySQL_Query query_mem = ESP32_MySQL_Query(&conn);

  // Execute the query
  ESP32_MYSQL_DISPLAY(qquery);

  if ( !query_mem.execute(qquery.c_str()) )
  {
    ESP32_MYSQL_DISPLAY("Querying error");
    return;
  }
  column_names *cols = query_mem.get_columns();
  for (int f = 0; f < cols->num_fields; f++) {
    Serial.print(cols->fields[f]->name);
    if (f < cols->num_fields - 1) {
      Serial.print(',');
    }


    row_values *row = NULL;
    do {
      row = query_mem.get_next_row();
      if (row != NULL) {
        for (int f = 0; f < cols->num_fields; f++) {
          Serial.print(row->values[f]);
          if (f < cols->num_fields - 1) {
            Serial.print(',');
          }
        }
        Serial.println();
      }
    } while (row != NULL);
    // Deleting the cursor also frees up memory used
    // delete query_mem;

  }

}
*/
int rst_cntr=0;
void loop()
{
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    // выводим сообщение о получении пакета
    Serial.print("Received packet '");
    byte bufferBytes[24];
    int k = 0;
    while (LoRa.available())
    {
      
      bufferBytes[k] = LoRa.read();
      Serial.println(bufferBytes[k]);
      k++;
    }
    memcpy(&telem_packet, &bufferBytes, 24);
    // выводим RSSI пакета
    Serial.print("' with RSSI ");
  // Serial.println(LoRa.packetRssi());
   // ESP32_MYSQL_DISPLAY("Connecting...");
 //   timeClient.update();
    Serial.println(telem_packet.device);
    Serial.println(telem_packet.msg);
    Serial.println(telem_packet.hum);
    Serial.println(telem_packet.tempp);
    //Serial.println(timeClient.getFormattedTime().c_str());
    if (conn.connectNonBlocking(server, server_port, user, password) != RESULT_FAIL)
    {
      delay(500);
      insertData(telem_packet.device, telem_packet.msg, "CURRENT_TIMESTAMP", telem_packet.hum, telem_packet.tempp, telem_packet.voltage, LoRa.packetRssi());
     // runQuery();
      conn.close();                     // close the connection
      //ESP.restart();  
      rst_cntr++;
      if(telem_packet.msg>50)
      {
        ESP.restart(); 
      }
    }
    else
    {
      ESP32_MYSQL_DISPLAY("\nConnect failed. Trying again on next iteration.");
     // ESP.restart();  
    }
   // ESP32_MYSQL_DISPLAY("\nSleeping...");
   // ESP32_MYSQL_DISPLAY("================================================");
  }
  delay(100);
  Serial.print("...");
   if (ota.checkUpdate())
  {
     ESP.restart();
  }
   if (ota.tick())
   {
        ESP.restart();
    }

}
