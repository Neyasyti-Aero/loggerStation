#include <Arduino.h>
#include "lib/AutoOTA.h"

#include "Credentials.h"
#include <SPI.h>
#include "lib/LoRa/LoRa.h"
#include <WiFiUdp.h>
#include <HTTPClient.h>

#define ESP32_MYSQL_DEBUG_PORT Serial
#define _ESP32_MYSQL_LOGLEVEL_ 5
#include "lib/ESP32_MySQL/ESP32_MySQL.h"

#define ss 5
#define rst 14
#define dio0 2

IPAddress server(91, 197, 98, 72);
uint16_t server_port = 3306;

char default_database[] = "test";
char default_table[]    = "logdata";
ESP32_MySQL_Connection conn((Client *)&client);

char printBuf[512];

unsigned long last_packet_ms = 0;

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
uint32_t chipId = 0;

void insertData(uint32_t device_id, uint32_t msg_id, const char* time, float humidity, float temperature, float battery, int RSSII)
{
  char query[512];
  sprintf(query, "INSERT INTO `test`.`logdata` (`device_id`, `msg_id`, `time`, `humidity`, `temperature`, `battery`, `rssi`, `station_id`, `chip_id`) VALUES ('%i', '%i', CURRENT_TIMESTAMP, '%f', '%f', '%f', '%i', '%i', '%i')", device_id, msg_id, humidity, temperature, battery, RSSII, 32, chipId);

  if (!conn.connected())
  {
    ESP32_MYSQL_DISPLAY("Disconnected from Server. Can't insert");
    HandleDatabaseIssue();
    return;
  }

  ESP32_MySQL_Query query_mem = ESP32_MySQL_Query(&conn);
  if (!query_mem.execute(query))
  {
    ESP32_MYSQL_DISPLAY("Querying (insert) error");
    HandleDatabaseIssue();
  }
}

void getMyIpAddress(char *ip_address, char *port = NULL)
{
  ESP32_MySQL_Query query_mem = ESP32_MySQL_Query(&conn);

  if (!conn.connected())
  {
    ESP32_MYSQL_DISPLAY("Disconnected from Server. Can't get ip address");
    HandleDatabaseIssue();
    return;
  }

  if (!query_mem.execute("SELECT SUBSTRING_INDEX(`HOST`, ':', 1) AS `ip_address`, SUBSTRING_INDEX(`HOST`, ':', -1) AS `port` FROM `information_schema`.`processlist` WHERE `ID` = CONNECTION_ID()"))
  {
    ESP32_MYSQL_DISPLAY("Querying (select) error");
    HandleDatabaseIssue();
    return;
  }
  
  column_names *cols = query_mem.get_columns();
  row_values *row = query_mem.get_next_row();
  
  if (row != NULL)
  {
    sprintf(ip_address, "%s", row->values[0]);
    if (port != NULL)
    {
      sprintf(port, row->values[1]);
    }
  }
  else
  {
    sprintf(ip_address, "UNKNOWN");
    if (port != NULL)
    {
      sprintf(port, "UNKNOWN");
    }
  }
}

bool testDBconnection()
{
  ESP32_MYSQL_DISPLAY3("Test connection to SQL Server @", server, ", Port =", server_port);
  ESP32_MYSQL_DISPLAY5("User =", user, ", PW =", password, ", DB =", default_database);

  if (conn.connectNonBlocking(server, server_port, user, password) != RESULT_FAIL)
  {
    delay(100);
    ESP32_MYSQL_DISPLAY3("Successfully connected to SQL Server @", server, ", Port =", server_port);
    conn.close();
    return true;
  }
  else
  {
    ESP32_MYSQL_DISPLAY("\r\nDatabase simple connection test failed!");
    return false;
  }
}

bool testDBquery()
{
  ESP32_MYSQL_DISPLAY3("Test select query to SQL Server @", server, ", Port =", server_port);

  if (conn.connectNonBlocking(server, server_port, user, password) != RESULT_FAIL)
  {
    delay(100);
    char ip[32], port[32];
    getMyIpAddress(ip, port);
    ESP32_MYSQL_DISPLAY3("IP =", ip, "; Port =", port);
    conn.close();
    return true;
  }
  else
  {
    ESP32_MYSQL_DISPLAY("\r\nDatabase test query failed!");
    return false;
  }
}

void HandleWiFiDisconnected()
{
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  int cntr = 0;
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(100);
    Serial.print(".");
    cntr++;
    if (cntr > 600)
    {
      Serial.println("\r\nCan't connect to WiFi network. ESP is about to be restarted\r\n");
      ESP.restart();
    }
  }
}

bool DetectOlaxMT10()
{
  HTTPClient http;

  // OLAX MT10
  http.begin("http://192.168.0.1/reqproc/proc_get?multi_data=1&cmd=modem_main_state%2Csignalbar%2Cnetwork_type%2Cnv_rsrq");

  // Send HTTP GET request
  int httpGetResponseCode = http.GET();

  if (httpGetResponseCode == 200)
  {
    ESP32_MYSQL_DISPLAY1("Identified OLAX MT10\r\nHTTP Response code:", httpGetResponseCode);
    String payload = http.getString();
    Serial.println(payload);
  }
  else
  {
    ESP32_MYSQL_DISPLAY1("Failed to identify OLAX MT10\r\nHTTP Response code:", httpGetResponseCode);
    String payload = http.getString();
    Serial.println(payload);
    http.end();
    return false;
  }

  // Free resources
  http.end();

  return true;
}

bool DetectTenda()
{
  HTTPClient http;

  // Tenda
  http.begin("http://192.168.0.1/goform/goform_get_cmd_process?multi_data=1&cmd=modem_main_state%2Csignalbar%2Cnetwork_type%2Cnv_rsrq");

  // Send HTTP GET request
  int httpGetResponseCode = http.GET();

  if (httpGetResponseCode == 200)
  {
    ESP32_MYSQL_DISPLAY1("Identified Tenda\r\nHTTP Response code:", httpGetResponseCode);
    String payload = http.getString();
    Serial.println(payload);
  }
  else
  {
    ESP32_MYSQL_DISPLAY1("Failed to identify Tenda\r\nHTTP Response code:", httpGetResponseCode);
    String payload = http.getString();
    Serial.println(payload);
    http.end();
    return false;
  }

  // Free resources
  http.end();

  return true;
}

void HandleDatabaseIssue()
{
  ESP32_MYSQL_DISPLAY0("\r\nDatabase issue. Router is about to be restarted\r\n");

  // Identify router type
  HTTPClient http;
  WiFiClient client;

  if (DetectOlaxMT10())
  {
    // Reboot identified OLAX MT10
    http.begin(client, "http://192.168.0.1/reqproc/proc_post");

    // Body to send with HTTP POST
    String httpRequestData = "goformId=REBOOT_DEVICE";
  }
  else if (DetectTenda())
  {
    // Reboot identified Tenda
    http.begin(client, "http://192.168.0.1/goform/goform_set_cmd_process");

    // Body to send with HTTP POST
    String httpRequestData = "goformId=REBOOT_DEVICE";
  }
  else
  {
    ESP32_MYSQL_DISPLAY0("Failed to identify router type. Impossible to reboot");
    // Free resources
    http.end();
    return;
  }

  // Send HTTP POST request
  int httpPostResponseCode = http.POST(httpRequestData);

  ESP32_MYSQL_DISPLAY1("\r\nReboot requested for identified router\r\nHTTP Response code:", httpPostResponseCode);

  // Free resources
  http.end();

}

void HandleLoraIssue()
{
  Serial.print("\r\nLoRa reinitialization started");
  LoRa.setPins(ss, rst, dio0);

  int cntr = 0;
  last_packet_ms = millis(); // set to current timestamp to avoid endless reinit sequence
  while (!LoRa.begin(433E6))
  {
    Serial.print(".");
    delay(300);
    cntr++;
    if (cntr > 20)
    {
      Serial.println("\r\nLoRa initialization failed!");
      if (conn.connectNonBlocking(server, server_port, user, password) != RESULT_FAIL)
      {
        delay(500);
        insertData(0, 0, "CURRENT_TIMESTAMP", 0, 0, 0, 0);
        conn.close();
      }
      else
      {
        ESP32_MYSQL_DISPLAY("\r\nConnect failed for LoRa fail's report");
        HandleDatabaseIssue();
      }
      ESP.restart();
    }
  }

  Serial.println("\r\nLoRa initialized!");

  LoRa.setSyncWord(0x12);
  LoRa.setSpreadingFactor(12);
  LoRa.setGain(6);
  LoRa.receive();
  LoRa.setCodingRate4(8);
  LoRa.enableCrc();
}

void CheckHealth()
{
  // Check network health
  if (WiFi.status() != WL_CONNECTED)
  {
    // Lost connection to WiFi network
    // Try to reconnect
    // Restart ESP32 if unsuccessful
    Serial.print("Lost connection to WiFi network. Trying to reconnect...");
    HandleWiFiDisconnected();

    // No restart => Connected to WiFi network successfully

    // Test database connection
    if (!testDBconnection())
    {
      // Failed Db connection test
      // Restart router here and return
      Serial.print("No database connection. Restarting router...");
      HandleDatabaseIssue();
      return;
    }
    if (!testDBquery())
    {
      // Failed to perform select-query
      // Restart router here and return
      Serial.print("Error during DB test. Restarting router...");
      HandleDatabaseIssue();
      return;
    }
  }

  // All network-related tests were passed

  // Check LoRa's health
  // If the last packet recieved was recieved more than 20 minuted ago => LoRa should restart
  if (millis() - last_packet_ms > 20 * 60 * 1000 || millis() < last_packet_ms)
  {
    Serial.print("LoRa restart required...");
    HandleLoraIssue();
  }

  // Restart ESP32 every 24 hours
  if (millis() > 24 * 60 * 60 * 1000)
  {
    Serial.print("\r\nProfilactical restart of ESP32...\r\n");
    ESP.restart();
  }

  // All tests were passed
}

void setup()
{
  // Get ChipId of this device
  for (int i = 0; i < 17; i = i + 8) {
    chipId |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
  }

  Serial.begin(115200);
  while (!Serial && millis() < 5000); // wait for serial port to connect

  delay(3000); // make a small pause

  // Say hello
  Serial.print("\r\n\r\n\r\nHello! I am a device with id #");
  Serial.println(chipId);

  // Print the current firmware version
  Serial.print("Version: ");
  Serial.println(ota.version());

  // Connect to WiFi network
  // ESP32 will be restarted if unsuccessful
  Serial.print("Connecting to WiFi network...");
  HandleWiFiDisconnected();

  // No restart => Successful connection to WiFi network
  ESP32_MYSQL_DISPLAY1("\r\nConnected to WiFi. My local IP-address is:", WiFi.localIP());

  // Check for FW updates
  String ver, notes;
  bool should_update_fw = ota.checkUpdate(&ver, &notes);
  Serial.println(ver);
  Serial.println(notes);
  if (should_update_fw) {
    Serial.println("OTA: Firmware is about to be updated");
    ota.updateNow();
  }

  // Test database availability
  if (testDBconnection() && testDBquery())
  {
    ESP32_MYSQL_DISPLAY0("Both DB tests were passed!");
  }
  else
  {
    ESP32_MYSQL_DISPLAY0("DB test were NOT passed!");
  }

  // Init LoRa
  // ESP32 will be restarted if unsuccessful
  HandleLoraIssue();

  // setup completed
  Serial.print("Waiting for packets...");
}

void loop()
{
  CheckHealth();

  int packetSize = LoRa.parsePacket();

  if (packetSize)
  {
    if (packetSize % sizeof(telem_packet) != 0)
    {
      sprintf(printBuf, "\r\nReceived new packet, size = %u bytes\r\nThe size is incorrect!\r\nLora is about to be restarted...", packetSize);
      Serial.println(printBuf);
      HandleLoraIssue();
      return;
    }

    last_packet_ms = millis();
    sprintf(printBuf, "\r\nReceived new packet, size = %u bytes, hex-payload: \"", packetSize);
    Serial.print(printBuf);

    byte bufferBytes[sizeof(telem_packet)];
    for (int k = 0; LoRa.available() && k < sizeof(telem_packet); k++)
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
      delay(50);
      insertData(telem_packet.device, telem_packet.msg, "CURRENT_TIMESTAMP", telem_packet.hum, telem_packet.tempp, telem_packet.voltage, packetRssi);
      conn.close();
    }
    else
    {
      ESP32_MYSQL_DISPLAY("\r\nConnect failed. Trying again on next iteration.");
      HandleDatabaseIssue();
    }
  }

  delay(100);
  Serial.print(".");

  if (ota.tick()) {
    Serial.println("\r\nOTA: ");
    Serial.println((int)ota.getError());
  }
}
