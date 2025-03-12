#include <Arduino.h>
#include "lib/AutoOTA.h"

#include "Credentials.h"
#include <SPI.h>
#include "lib/LoRa/LoRa.h"
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include <time.h>

#define ESP32_MYSQL_DEBUG_PORT Serial
// max loglevel = 5
#define _ESP32_MYSQL_LOGLEVEL_ 2
#include "lib/ESP32_MySQL/ESP32_MySQL.h"

AutoOTA ota("5.0", "https://raw.githubusercontent.com/Neyasyti-Aero/loggerStation/refs/heads/main/project.json");

#define ss 5
#define rst 14
#define dio0 2

#define DB_ESP_RESTARTED 1
#define DB_LORA_ERROR 2
#define DB_LORA_RESTART_TIMEOUT 3
#define DB_ESP_RESTART_TIMEOUT 4
#define DB_ESP_RESTART_MANUAL 5

IPAddress server(91, 197, 98, 72);
uint16_t server_port = 3306;

char default_database[] = "test";
char default_table[]    = "logdata";
ESP32_MySQL_Connection conn((Client *)&client);
uint8_t ssid_num = 0;

char printBuf[512];

unsigned long last_packet_ms = 0;
unsigned long last_timestamp_ms = 0;

time_t now;
tm tm;

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

void reportToDatabase(uint8_t code)
{
  if (conn.connectNonBlocking(server, server_port, user, password) != RESULT_FAIL)
  {
    delay(500);
    insertData(0, code, "CURRENT_TIMESTAMP", 0, 0, 0, 0);
    conn.close();
  }
  else
  {
    ESP32_MYSQL_DISPLAY("\r\nConnect failed for LoRa fail's report");
    HandleDatabaseIssue();
  }
}

void printMacAddress()
{
  // the MAC address of the Wifi shield
  byte mac[6];

  // print the MAC address:
  WiFi.macAddress(mac);
  Serial.print("MAC: ");
  Serial.print(mac[5], HEX);
  Serial.print(":");
  Serial.print(mac[4], HEX);
  Serial.print(":");
  Serial.print(mac[3], HEX);
  Serial.print(":");
  Serial.print(mac[2], HEX);
  Serial.print(":");
  Serial.print(mac[1], HEX);
  Serial.print(":");
  Serial.println(mac[0], HEX);
}

void printEncryptionType(int thisType) {
  // read the encryption type and print out the name:
  switch (thisType) {
    case WIFI_AUTH_OPEN:            Serial.println("open"); break;
    case WIFI_AUTH_WEP:             Serial.println("WEP"); break;
    case WIFI_AUTH_WPA_PSK:         Serial.println("WPA"); break;
    case WIFI_AUTH_WPA2_PSK:        Serial.println("WPA2"); break;
    case WIFI_AUTH_WPA_WPA2_PSK:    Serial.println("WPA+WPA2"); break;
    case WIFI_AUTH_WPA2_ENTERPRISE: Serial.println("WPA2-EAP"); break;
    case WIFI_AUTH_WPA3_PSK:        Serial.println("WPA3"); break;
    case WIFI_AUTH_WPA2_WPA3_PSK:   Serial.println("WPA2+WPA3"); break;
    case WIFI_AUTH_WAPI_PSK:        Serial.println("WAPI"); break;
    default:                        Serial.println("unknown");
  }
}

void listNetworks()
{
  // scan for nearby networks:
  Serial.println("** Scan Networks **");
  /*
  int16_t scanNetworks(
    bool async = false, bool show_hidden = false, bool passive = false, uint32_t max_ms_per_chan = 300, uint8_t channel = 0, const char *ssid = nullptr,
    const uint8_t *bssid = nullptr
  );
  */
  int numSsid = WiFi.scanNetworks(false, false, false, 300, 0, nullptr, nullptr);
  if (numSsid == WIFI_SCAN_FAILED)
  {
    Serial.println("Couldn't get a WiFi connection");
    return;
  }

  // print the list of networks seen:
  Serial.print("Number of available WiFi networks: ");
  Serial.println(numSsid);

  // print the network number and name for each network found:
  for (int thisNet = 0; thisNet < numSsid; thisNet++)
  {
    Serial.print(thisNet + 1);
    Serial.print(") ");
    Serial.print(WiFi.SSID(thisNet));
    Serial.print("\tChannel: ");
    Serial.print(WiFi.channel(thisNet));
    Serial.print("\tSignal: ");
    Serial.print(WiFi.RSSI(thisNet));
    Serial.print(" dBm");
    Serial.print("\tEncryption: ");
    printEncryptionType(WiFi.encryptionType(thisNet));
  }
}

// true => found
bool searchNetwork(char* szSSID)
{
  // scan for nearby networks:
  uint8_t numSsid = WiFi.scanNetworks();

  for (uint8_t thisNet = 0; thisNet < numSsid; thisNet++)
  {
    if (strcmp(szSSID, WiFi.SSID(thisNet).c_str()) == 0) // match found
    {
      return true;
    }
  }

  return false;
}

void HandleWiFiDisconnected()
{
  // Do some kind of "restart"
  WiFi.disconnect();
  WiFi.mode(WIFI_OFF);
  WiFi.mode(WIFI_STA);

  delay(100);
  Serial.println("");
  printMacAddress();
  
  for (uint8_t attempt_num = 0; attempt_num < 5; attempt_num++)
  {
    Serial.println("================");
    Serial.print("== ATTEMPT: ");
    Serial.print(attempt_num + 1);
    Serial.println(" ==");
    Serial.println("================");

    listNetworks();
    WiFi.scanDelete();

    for (uint8_t ssid_temp = 0; ssid_temp < MAX_SSID_NAMES; ssid_temp++)
    {
      Serial.print("Trying to connect: ");
      Serial.println(ssid[ssid_temp]);

      if (!searchNetwork(ssid[ssid_temp])) // ssid not found -> skip
      {
        WiFi.scanDelete();
        Serial.print("Not found SSID (skip): ");
        Serial.println(ssid[ssid_temp]);
        continue;
      }
      else // found this ssid, try to connect
      {
        Serial.print("Found SSID (connect): ");
        Serial.println(ssid[ssid_temp]);
      }
      
      WiFi.begin(ssid[ssid_temp], pass);
      int cntr = 0;
      while (WiFi.status() != WL_CONNECTED)
      {
        delay(100);
        Serial.print(".");
        cntr++;
        if (cntr > 1800)
        {
          Serial.print("Can't connect to SSID (timeout): ");
          Serial.println(ssid[ssid_temp]);
          WiFi.disconnect();
          break;
        }

        // Got WiFi connection
        // save the ssid num
        Serial.print("Connected to: ");
        Serial.println(ssid[ssid_temp]);
        ssid_num = ssid_temp;
        return;
      }
    }

    delay(10000);
  }

  Serial.println("\r\nCan't connect to WiFi network. ESP is about to be restarted\r\n");
  delay(3000);
  ESP.restart();
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

  // Tenda 4G185
  http.begin("http://192.168.0.1/goform/goform_get_cmd_process?multi_data=1&cmd=modem_main_state%2Csignalbar%2Cnetwork_type");

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
    http.end();
    return false;
  }

  // Free resources
  http.end();

  return true;
}

// Detect MIFI 4G
bool DetectMIFI()
{
  HTTPClient http;
  WiFiClient client;

  http.begin(client, "http://192.168.100.1/himiapi/json");

  // Body to send with HTTP POST
  String httpRequestData = String("{\"cmdid\":\"getconfig\",\"sessionId\":null}");

  // Send HTTP POST request
  int httpPostResponseCode = http.POST(httpRequestData);

  if (httpPostResponseCode == 200)
  {
    ESP32_MYSQL_DISPLAY1("Identified MIFI\r\nHTTP Response code:", httpPostResponseCode);
    String payload = http.getString();
    Serial.println(payload);
  }
  else
  {
    ESP32_MYSQL_DISPLAY1("Failed to identify MIFI\r\nHTTP Response code:", httpPostResponseCode);
    http.end();
    return false;
  }

  // Free resources
  http.end();

  return true;
}

// Detect LTE 4G CPE
bool Detect4GCPE()
{
  HTTPClient http;
  WiFiClient client;

  http.begin(client, "http://192.168.199.1/goform/goform_get_cmd_process?multi_data=1&isTest=false&cmd=modem_main_state&_=1741741559430");

  // Send HTTP POST request
  int httpPostResponseCode = http.GET();

  if (httpPostResponseCode == 200)
  {
    ESP32_MYSQL_DISPLAY1("Identified 4G CPE\r\nHTTP Response code:", httpPostResponseCode);
    String payload = http.getString();
    Serial.println(payload);
  }
  else
  {
    ESP32_MYSQL_DISPLAY1("Failed to identify 4G CPE\r\nHTTP Response code:", httpPostResponseCode);
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
  String httpRequestData;

  if (DetectOlaxMT10())
  {
    // Reboot identified OLAX MT10
    http.begin(client, "http://192.168.0.1/reqproc/proc_post");

    // Body to send with HTTP POST
    httpRequestData = "goformId=REBOOT_DEVICE";
  }
  else if (DetectTenda())
  {
    // Reboot identified Tenda
    http.begin(client, "http://192.168.0.1/goform/goform_set_cmd_process");

    // Body to send with HTTP POST
    httpRequestData = "goformId=REBOOT_DEVICE";
  }
  else if (DetectMIFI())
  {
    // login
    http.begin(client, "http://192.168.100.1/himiapi/json");
    // Body to send with HTTP POST
    httpRequestData = "{\"cmdid\":\"login\",\"username\":\"admin\",\"password\":\"admin\",\"sessionId\":null}";

    int httpPostResponseCode = http.POST(httpRequestData);
    ESP32_MYSQL_DISPLAY1("LogIn Response code:", httpPostResponseCode);
    String payload = http.getString();
    Serial.println(payload);
    if (payload.length() < 48)
    {
      ESP32_MYSQL_DISPLAY1("Error, too short payload:", payload.length());
      http.end();
      return;
    }
    // {"session":"a3777608-f197-472c-970a-3036831cf5f6","reply":"ok"}
    // parse session id
    String sessionId = payload.substring(12, 48);
    ESP32_MYSQL_DISPLAY1("Got session id:", sessionId);

    http.end();
    http.begin(client, "http://192.168.100.1/himiapi/json");

    httpRequestData = String("{cmdid: \"rebootcmd\", params: \"reboot\", sessionId: \"") + sessionId + String("\"}");
    Serial.println(httpRequestData);
  }
  else if (Detect4GCPE())
  {
    // login
    http.begin(client, "http://192.168.199.1/goform/goform_set_cmd_process");

    // Body to send with HTTP POST
    httpRequestData = String("isTest=false&goformId=LOGIN&password=YWRtaW4KYWRtaW4%3D");
    const char* headers[] = {"Set-Cookie"};
    http.collectHeaders(headers, sizeof(headers)/ sizeof(headers[0]));
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    // Send HTTP POST request
    int httpPostResponseCode = http.POST(httpRequestData);
    
    ESP32_MYSQL_DISPLAY1("Logged in, status code:", httpPostResponseCode);
    Serial.println(http.getString());

    if (httpPostResponseCode == 200)
    {
      String cookie = http.header("Set-Cookie");
      Serial.println(cookie);

      if (cookie.length() < 35 || cookie.indexOf(';') < 12)
      {
        ESP32_MYSQL_DISPLAY1("Error, got invalid Set-Cookie header with length:", cookie.length());
        ESP32_MYSQL_DISPLAY1("Error, got invalid Set-Cookie header, index found:", cookie.indexOf(';'));
        http.end();
        return;
      }
      // JSESSIONID=f41ottp186n9mkmcsyg3dvnw;Path=/
      // JSESSIONID=zewe05zl41lg1leozqwia74eh;Path=/
      // parse session id
      String sessionId = cookie.substring(11, cookie.indexOf(';'));
      ESP32_MYSQL_DISPLAY1("Got session id:", sessionId);

      http.end();
      http.begin(client, "http://192.168.199.1/goform/goform_set_cmd_process");

      http.addHeader("Cookie", (String("JSESSIONID=") + sessionId).c_str());
      http.addHeader("Content-Type", "application/x-www-form-urlencoded");
      httpRequestData = String("isTest=false&goformId=REBOOT_DEVICE");
    }
  }
  else
  {
    ESP32_MYSQL_DISPLAY0("Failed to identify router type. Impossible to reboot");
    ESP32_MYSQL_DISPLAY0("ESP is about to be restarted...");
    delay(3000);
    ESP.restart();

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
      reportToDatabase(DB_LORA_ERROR);
      reportToDatabase(DB_ESP_RESTART_MANUAL);
      Serial.println("\r\nLoRa initialization failed!");
      Serial.println("\r\nESP is about to be restarted...");
      delay(3000);
      ESP.restart();
    }
  }

  Serial.println("\r\nLoRa initialized!");

  LoRa.setSyncWord(0x12);
  LoRa.setSpreadingFactor(12);
  LoRa.setGain(6);
  // doesn't work with old loggers => commented out
  // it has to be enabled for both sides
  //LoRa.enableLowDataRateOptimize();
  LoRa.receive();
  LoRa.setCodingRate4(8);
  LoRa.enableCrc();
}

void showTime()
{
  time(&now);
  localtime_r(&now, &tm);

  if (tm.tm_mday < 10)
  {
    Serial.print("0");
  }
  Serial.print(tm.tm_mday);
  Serial.print(".");
  if (tm.tm_mon < 10)
  {
    Serial.print("0");
  }
  Serial.print(tm.tm_mon + 1);
  Serial.print(".");
  Serial.print(tm.tm_year + 1900);
  Serial.print(" ");
  if (tm.tm_hour < 10)
  {
    Serial.print("0");
  }
  Serial.print(tm.tm_hour);
  Serial.print(":");
  if (tm.tm_min < 10)
  {
    Serial.print("0");
  }
  Serial.print(tm.tm_min);
  Serial.print(":");
  if (tm.tm_sec < 10)
  {
    Serial.print("0");
  }
  Serial.print(tm.tm_sec);
}

void CheckHealth()
{
  // Print current datetime every N minutes
  if (millis() - last_timestamp_ms > 1 * 60 * 1000 || millis() < last_timestamp_ms || last_timestamp_ms == 0)
  {
    Serial.println("");
    showTime();
    last_timestamp_ms = millis();
    Serial.println("");
  }

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
    reportToDatabase(DB_LORA_RESTART_TIMEOUT);
    Serial.print("LoRa restart required...");
    HandleLoraIssue();
  }

  // Restart ESP32 every 6 hours
  if (millis() > 6 * 60 * 60 * 1000)
  {
    reportToDatabase(DB_ESP_RESTART_TIMEOUT);
    Serial.print("\r\nProfilactical restart of ESP32...\r\n");
    delay(3000);
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

  // Configure NTP
  configTzTime("MSK-3", "0.ru.pool.ntp.org", "1.ru.pool.ntp.org", "2.ru.pool.ntp.org");

  // Connect to WiFi network
  // ESP32 will be restarted if unsuccessful
  Serial.print("Connecting to WiFi network...");
  HandleWiFiDisconnected();

  delay(3000); // make a small pause

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
    // log esp restart event
    reportToDatabase(DB_ESP_RESTARTED);
    ESP32_MYSQL_DISPLAY0("All DB tests were passed!");
  }
  else
  {
    ESP32_MYSQL_DISPLAY0("DB test were NOT passed!");
    HandleDatabaseIssue();
  }

  // Init LoRa
  // ESP32 will be restarted if unsuccessful
  HandleLoraIssue();

  Serial.println("");
  showTime();
  Serial.println("");

  // setup completed
  Serial.print("Waiting for packets...");
}

void loop()
{
  CheckHealth();

  int packetSize = LoRa.parsePacket();

  if (packetSize)
  {
    Serial.println("");
    showTime();
    Serial.println("");
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
    float packetSnr = LoRa.packetSnr();
    sprintf(printBuf, "device_id: %u\r\nmsg_id: %u\r\n\humidity: %f\r\ntemperature: %f\r\npressure: %f\r\nbattery: %f\r\nrssi: %i\r\nsnr: %f\r\n", telem_packet.device, telem_packet.msg, telem_packet.hum, telem_packet.tempp, telem_packet.pressr, telem_packet.voltage, packetRssi, packetSnr);
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
    Serial.print("\r\nOTA: ");
    Serial.println((int)ota.getError());
  }
}
