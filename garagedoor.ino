/*
 * ESP32 Telegram Garage Door Controller
 * 
 * Commands:
 * /open - Opens door
 * /close - Closes door
 * /status - Current door status
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include <time.h>

// Include secrets file
#include "secrets.h"

// Door states
enum DoorState { DOOR_CLOSED, DOOR_OPEN, DOOR_UNKNOWN };

// Pins
const int RELAY_PIN = 26;
const int LIMIT_SWITCH_PIN = 27;
const int WARNING_LIGHT_PIN = 13;
const int BUZZER_PIN = 12;

// Default hostname if not in secrets.h
#ifndef DEVICE_HOSTNAME
  #define DEVICE_HOSTNAME "GarageDoorBot"
#endif

// Timing
const unsigned long PRE_OPERATION_WARNING_TIME = 2000;
const unsigned long DOOR_ACTIVATION_DURATION = 2000;
const unsigned long OPERATION_TIMEOUT = 20000;

// Event tracking
struct DoorEvent {
  DoorState state;
  unsigned long timestamp;
  String source;
};

DoorEvent lastEvent = {DOOR_UNKNOWN, 0, "boot"};

// State variables
DoorState currentDoorState = DOOR_UNKNOWN;
bool doorOperationInProgress = false;

// Debounce
unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 50;
int lastSwitchState = HIGH;

// Bot setup
WiFiClientSecure client;
UniversalTelegramBot bot(TELEGRAM_BOT_TOKEN, client);
unsigned long botLastCheckTime = 0;
const unsigned long BOT_CHECK_INTERVAL = 1000;

// Get formatted time string
String getFormattedTime(unsigned long timestamp) {
  time_t rawtime = timestamp;
  struct tm * timeinfo;
  char buffer[80];
  
  timeinfo = localtime(&rawtime);
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
  return String(buffer);
}

// Record door events
void recordDoorEvent(DoorState state, String source) {
  time_t now;
  time(&now);
  
  lastEvent.state = state;
  lastEvent.timestamp = now;
  lastEvent.source = source;
  
  Serial.println("Door event: " + 
                 String(state == DOOR_OPEN ? "OPEN" : "CLOSED") + 
                 " via " + source + 
                 " at " + getFormattedTime(now));
}

// Read door state with debouncing
DoorState getDoorState() {
  int reading = digitalRead(LIMIT_SWITCH_PIN);
  
  if (reading != lastSwitchState) {
    lastDebounceTime = millis();
  }
  
  DoorState currentState = currentDoorState;
  
  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading == HIGH) {  // Switch not pressed - door open
      currentState = DOOR_OPEN;
    } else {               // Switch pressed - door closed
      currentState = DOOR_CLOSED;
    }
  }
  
  lastSwitchState = reading;
  return currentState;
}

// Run warnings before door operation
void runPreOperationWarning() {
  Serial.println("Running warning...");
  
  unsigned long startTime = millis();
  
  while (millis() - startTime < PRE_OPERATION_WARNING_TIME) {
    digitalWrite(WARNING_LIGHT_PIN, HIGH);
    delay(200);
    digitalWrite(WARNING_LIGHT_PIN, LOW);
    delay(200);
    
    tone(BUZZER_PIN, 2000, 100);
    delay(300);
  }
  
  digitalWrite(WARNING_LIGHT_PIN, LOW);
  noTone(BUZZER_PIN);
}

// Activate door relay
void triggerGarageDoor() {
  runPreOperationWarning();
  
  digitalWrite(RELAY_PIN, HIGH);
  delay(DOOR_ACTIVATION_DURATION);
  digitalWrite(RELAY_PIN, LOW);
}

// Process Telegram messages
void handleNewMessages(int numNewMessages) {
  for (int i = 0; i < numNewMessages; i++) {
    String chat_id = bot.messages[i].chat_id;
    String user_id = bot.messages[i].from_id;
    String text = bot.messages[i].text;
    
    // Auth check
    if (chat_id != String(AUTHORIZED_CHAT_ID)) {
      bot.sendMessage(chat_id, "Unauthorized: Commands must be sent from the authorized chat only", "");
      continue;
    }
    
    // Process commands
    if (text == "/start" || text == "/help") {
      String welcome = "Welcome to Garage Door Bot.\n\n";
      welcome += "Commands:\n";
      welcome += "/open - Open the garage door\n";
      welcome += "/close - Close the garage door\n";
      welcome += "/status - Get current door status\n";
      welcome += "/help - Show this message\n";
      welcome += "/log - Show recent activity\n";
      welcome += "\nConfig:\n";
      welcome += "- Warning time: " + String(PRE_OPERATION_WARNING_TIME/1000) + " seconds\n";
      welcome += "- Door activation: " + String(DOOR_ACTIVATION_DURATION) + "ms\n";
      welcome += "- Device name: " + String(DEVICE_HOSTNAME) + "\n";
      welcome += "- IP: " + WiFi.localIP().toString() + "\n";
      bot.sendMessage(chat_id, welcome, "");
    }
    
    else if (text == "/log") {
      String logMessage = "📋 *Garage Door Activity Log*\n\n";
      
      logMessage += "*Current State:* ";
      logMessage += (currentDoorState == DOOR_OPEN) ? "🟢 OPEN" : "🔴 CLOSED";
      logMessage += "\n\n";
      
      logMessage += "*Last Activity:*\n";
      logMessage += "• Action: ";
      logMessage += (lastEvent.state == DOOR_OPEN) ? "Door Opened" : "Door Closed";
      logMessage += "\n• Time: ";
      logMessage += getFormattedTime(lastEvent.timestamp);
      logMessage += "\n• Source: ";
      logMessage += lastEvent.source;
      logMessage += "\n\n";
      
      logMessage += "*System Info:*\n";
      logMessage += "• Device: ";
      logMessage += DEVICE_HOSTNAME;
      logMessage += "\n• IP: ";
      logMessage += WiFi.localIP().toString();
      logMessage += "\n• Uptime: ";
      unsigned long uptime = millis() / 1000;
      int days = uptime / 86400;
      int hours = (uptime % 86400) / 3600;
      int minutes = (uptime % 3600) / 60;
      int seconds = uptime % 60;
      
      if (days > 0) logMessage += String(days) + "d ";
      if (hours > 0) logMessage += String(hours) + "h ";
      logMessage += String(minutes) + "m " + String(seconds) + "s";
      
      time_t now;
      time(&now);
      logMessage += "\n• Current time: ";
      logMessage += getFormattedTime(now);
      
      bot.sendMessage(chat_id, logMessage, "Markdown");
    }
    
    else if (text == "/status") {
      currentDoorState = getDoorState();
      
      String status = "🚪 Garage Door Status:\n\n";
      
      if (currentDoorState == DOOR_OPEN) {
        status += "• Current state: 🟢 OPEN\n";
      } else if (currentDoorState == DOOR_CLOSED) {
        status += "• Current state: 🔴 CLOSED\n";
      } else {
        status += "• Current state: ⚠️ UNKNOWN\n";
      }
      
      status += "\nLast door activity:\n";
      status += "• Action: ";
      status += (lastEvent.state == DOOR_OPEN) ? "Opened" : "Closed";
      status += "\n• Time: ";
      status += getFormattedTime(lastEvent.timestamp);
      status += "\n• Triggered by: ";
      status += lastEvent.source;
      
      time_t now;
      time(&now);
      status += "\n\nStatus checked: ";
      status += getFormattedTime(now);
      
      bot.sendMessage(chat_id, status, "");
    }
    
    else if (text == "/open") {
      currentDoorState = getDoorState();
      
      if (currentDoorState == DOOR_OPEN) {
        bot.sendMessage(chat_id, "Garage door is already open!", "");
      } else {
        bot.sendMessage(chat_id, "⚠️ WARNING: Opening garage door in " + String(PRE_OPERATION_WARNING_TIME/1000) + " seconds...", "");
        
        doorOperationInProgress = true;
        triggerGarageDoor();
        
        unsigned long startTime = millis();
        bool success = false;
        
        while (millis() - startTime < OPERATION_TIMEOUT) {
          delay(500);
          if (getDoorState() == DOOR_OPEN) {
            success = true;
            break;
          }
        }
        
        doorOperationInProgress = false;
        
        if (success) {
          bot.sendMessage(chat_id, "✅ Garage door successfully opened!", "");
          currentDoorState = DOOR_OPEN;
          recordDoorEvent(DOOR_OPEN, "telegram");
        } else {
          bot.sendMessage(chat_id, "⚠️ Failed to open garage door within timeout period. Please check manually.", "");
        }
      }
    }
    
    else if (text == "/close") {
      currentDoorState = getDoorState();
      
      if (currentDoorState == DOOR_CLOSED) {
        bot.sendMessage(chat_id, "Garage door is already closed!", "");
      } else {
        bot.sendMessage(chat_id, "⚠️ WARNING: Closing garage door in " + String(PRE_OPERATION_WARNING_TIME/1000) + " seconds...", "");
        
        doorOperationInProgress = true;
        triggerGarageDoor();
        
        unsigned long startTime = millis();
        bool success = false;
        
        while (millis() - startTime < OPERATION_TIMEOUT) {
          delay(500);
          if (getDoorState() == DOOR_CLOSED) {
            success = true;
            break;
          }
        }
        
        doorOperationInProgress = false;
        
        if (success) {
          bot.sendMessage(chat_id, "✅ Garage door successfully closed!", "");
          currentDoorState = DOOR_CLOSED;
          recordDoorEvent(DOOR_CLOSED, "telegram");
        } else {
          bot.sendMessage(chat_id, "⚠️ Failed to close garage door within timeout period. Please check manually.", "");
        }
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("Starting garage door bot...");
  
  // Setup pins
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
  
  pinMode(LIMIT_SWITCH_PIN, INPUT_PULLUP);
  pinMode(WARNING_LIGHT_PIN, OUTPUT);
  digitalWrite(WARNING_LIGHT_PIN, LOW);
  pinMode(BUZZER_PIN, OUTPUT);
  
  // Connect WiFi
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(DEVICE_HOSTNAME);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  client.setCACert(TELEGRAM_CERTIFICATE_ROOT);
  
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
  
  // Setup time
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  
  Serial.print("Waiting for NTP time sync");
  time_t now = time(nullptr);
  while (now < 8 * 3600 * 2) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println("");
  
  // Check initial door state
  currentDoorState = getDoorState();
  Serial.print("Initial door state: ");
  Serial.println(currentDoorState == DOOR_OPEN ? "OPEN" : "CLOSED");
  
  recordDoorEvent(currentDoorState, "boot");
  
  // Send startup notification
  String startupMessage = "🚪 Garage Door Bot is online!\n";
  startupMessage += "Device: " + String(DEVICE_HOSTNAME) + "\n";
  startupMessage += "IP: " + WiFi.localIP().toString();
  bot.sendMessage(String(AUTHORIZED_CHAT_ID), startupMessage, "");
}

void loop() {
  // Check for new messages
  if (millis() - botLastCheckTime > BOT_CHECK_INTERVAL) {
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    
    if (numNewMessages > 0) {
      Serial.println("New message(s) received");
      handleNewMessages(numNewMessages);
    }
    
    botLastCheckTime = millis();
  }
  
  // Check door state
  DoorState newState = getDoorState();
  if (newState != currentDoorState) {
    // If state changed externally
    if (!doorOperationInProgress) {
      String stateChangeMsg = "⚠️ ALERT: Garage door ";
      stateChangeMsg += (newState == DOOR_OPEN) ? "OPENED" : "CLOSED";
      stateChangeMsg += " by external source";
      
      time_t now;
      time(&now);
      char timeStr[30];
      strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", localtime(&now));
      stateChangeMsg += "\nTimestamp: ";
      stateChangeMsg += timeStr;
      
      Serial.println(stateChangeMsg);
      recordDoorEvent(newState, "external");
      bot.sendMessage(String(AUTHORIZED_CHAT_ID), stateChangeMsg, "");
    }
    
    currentDoorState = newState;
    Serial.print("Door state: ");
    Serial.println(currentDoorState == DOOR_OPEN ? "OPEN" : "CLOSED");
  }
  
  delay(100);
}