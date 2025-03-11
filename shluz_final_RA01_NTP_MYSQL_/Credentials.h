/*
 * ESP32_MySQL - An optimized library for ESP32 to directly connect and execute SQL to MySQL database without intermediary.
 * 
 * Copyright (c) 2024 Syafiqlim
 * 
 * This software is released under the MIT License.
 * https://opensource.org/licenses/MIT
 */

#ifndef Credentials_h
#define Credentials_h

#define MAX_SSID_NAMES 2
char ssid0[] = "logger-m"; // network0 SSID (name) - the highest priority
char ssid1[] = "logger-r"; // network1 SSID (name)
char* ssid[MAX_SSID_NAMES] = {ssid0, ssid1};
char pass[] = "qwer1234"; // network password - stays the same for any router

char user[]         = "root";            // MySQL user login username
char password[]     = "HvYS0m4KOyyhEjk"; // MySQL user login password

#if MAX_SSID_NAMES < 1
#error Invalid 'MAX_SSID_NAMES' value!
#endif

#endif    //Credentials_h
