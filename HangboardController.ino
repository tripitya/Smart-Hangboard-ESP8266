#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <HX711_ADC.h>
#include <FastLED.h>
#include <EEPROM.h> // for saving the memory on the chip

// --- Hardware & Server Setup ---
LiquidCrystal_I2C lcd(0x27, 16, 2); 
ESP8266WebServer server(80);
DNSServer dnsServer;

// Pin Definitions
const int BUZZER_PIN = D0;
const int HX711_1_DT = D7;  // Left Data
const int HX711_1_SCK = D8; // Left Clock
const int HX711_2_DT = D5;  // Right Data
const int HX711_2_SCK = D6; // Right Clock

// --- LED Setup ---
#define LED_PIN     D4
#define NUM_LEDS    16
#define BRIGHTNESS  150
#define LED_TYPE    WS2812B
#define COLOR_ORDER GRB
CRGB leds[NUM_LEDS];

uint8_t rainbowHue = 0; 
unsigned long ledUpdateTimer = 0; 

// Load Cells
HX711_ADC LoadCell_1(HX711_1_DT, HX711_1_SCK); // Left
HX711_ADC LoadCell_2(HX711_2_DT, HX711_2_SCK); // Right

// --- Access Point Settings ---
const byte DNS_PORT = 53;
IPAddress apIP(192, 168, 4, 1);
const char* apSSID = "Hangboard";
const char* apPass = "password";

// --- Workout State Variables ---
enum WorkoutState { IDLE, PREP, HANG, REST, FINISHED, FAILED, WEIGH_WAIT, WEIGH_ACTIVE, WEIGH_DONE, MAX_HANG_ACTIVE};
WorkoutState currentState = IDLE;

// New Variables for Weighing
float weightSum = 0;
int weightSamples = 0;
float finalWeight = 0;

unsigned long stateStartTime = 0;
unsigned long displayUpdateTimer = 0;

int currentSet = 0;
int totalSets = 0;
int hangDuration = 0; 
int restDuration = 0; 

// --- User Selections ---
String currentMode = "Warmup";
String currentHolds = "Top_All";
String currentUser = "Guest";

// --- New Feature Tracking ---
float baselineWeight = 0.0;   // Fixes the Scale bug
int maxHangSeconds = 0;       // Tracks the stopwatch time

#define EEPROM_LEADERBOARD_START 8 // Starting address in memory
const int NUM_LEADERBOARD = 5;

// Memory template for a single record
struct Record {
  char name[11];
  int time;
};
Record leaderboard[NUM_LEADERBOARD]; // Array holding the Top 5

// --- Dummy Leaderboard Data (We will hook this to EEPROM later) ---
String topClimber1 = "Alex M."; int topTime1 = 124;
String topClimber2 = "Sarah J."; int topTime2 = 89;
String topClimber3 = "Chris P."; int topTime3 = 45;


// --- HELPER FUNCTIONS ---

void beep(int duration) {
  tone(BUZZER_PIN, 2000); 
  delay(duration);
  noTone(BUZZER_PIN); 
}

// Used to keep the dropdown menus on the correct choice
String getSelected(String currentVar, String targetVal) {
  if (currentVar == targetVal) return "selected";
  return "";
}

// Master LED Control Function for specific holds
void lightUpHolds(CRGB activeColor, CRGB inactiveColor) {
  // 1. Set entire board to the inactive/background color
  for(int i = 0; i < NUM_LEDS; i++) leds[i] = inactiveColor;

  // 2. Override specific LEDs based on selection
  // Top Row (0-7):    2-1-2-1-2 configuration
  // Bottom Row (8-15): 1-2-1-1-2-1 configuration
  
  if (currentHolds == "Top_All") {
    for(int i=0; i<=7; i++) leds[i] = activeColor;
  } 
  else if (currentHolds == "Top_Outer") {
    leds[0]=activeColor; leds[1]=activeColor; // Left Outer
    leds[6]=activeColor; leds[7]=activeColor; // Right Outer
  } 
  else if (currentHolds == "Top_Inner") {
    leds[2]=activeColor; // Left Inner
    leds[5]=activeColor; // Right Inner
  } 
  else if (currentHolds == "Top_Center") {
    leds[3]=activeColor; leds[4]=activeColor; // Center Jug
  } 
  else if (currentHolds == "Bottom_All") {
    for(int i=8; i<=15; i++) leds[i] = activeColor;
  } 
  else if (currentHolds == "Bottom_Outer") {
    leds[8]=activeColor; leds[15]=activeColor; 
  } 
  else if (currentHolds == "Bottom_Mid") {
    leds[9]=activeColor;  leds[10]=activeColor; // Mid Left
    leds[13]=activeColor; leds[14]=activeColor; // Mid Right
  } 
  else if (currentHolds == "Bottom_Inner") {
    leds[11]=activeColor; leds[12]=activeColor;
  }

  FastLED.show();
}

void updateLeaderboard(String name, int newTime) {
  if (name == "") name = "Unknown";
  
  int insertIndex = -1;
  // 1. Find if the new time beats anyone on the board
  for (int i = 0; i < NUM_LEADERBOARD; i++) {
    if (newTime > leaderboard[i].time) {
      insertIndex = i;
      break;
    }
  }
  
  // 2. If it made the Top 5, shift the lower scores down and save it
  if (insertIndex != -1) {
    for (int i = NUM_LEADERBOARD - 1; i > insertIndex; i--) {
      leaderboard[i] = leaderboard[i-1];
    }
    
    // Copy the new string name into the character array limit
    name.toCharArray(leaderboard[insertIndex].name, 11);
    leaderboard[insertIndex].time = newTime;

    // Burn it to permanent memory!
    EEPROM.put(EEPROM_LEADERBOARD_START, leaderboard);
    #if defined(ESP8266) || defined(ESP32)
    EEPROM.commit();
    #endif
  }
}

// --- HTML HOMEPAGE ---
void handleRoot() {

  String page = "<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
  page += "<style>";
  page += "body { font-family: 'Segoe UI', Tahoma, sans-serif; background-color: #eef2f5; margin: 0; padding: 20px; text-align: center; color: #333; }";
  page += ".card { background: white; border-radius: 15px; padding: 25px; box-shadow: 0 4px 15px rgba(0,0,0,0.1); max-width: 400px; margin: auto; }";
  page += "h2 { color: #2c3e50; margin-top: 0; }";
  page += "label { display: block; text-align: left; font-weight: bold; margin-bottom: 5px; color: #555; }";
  page += "select, input[type='text'] { width: 100%; box-sizing: border-box; font-size: 1.1rem; padding: 12px; border: 1px solid #ccc; border-radius: 8px; margin-bottom: 20px; background: #f9f9f9; }";
  page += ".btn { width: 100%; background-color: #007bff; color: white; padding: 15px; font-size: 1.2rem; font-weight: bold; border: none; border-radius: 8px; cursor: pointer; text-transform: uppercase; letter-spacing: 1px; transition: 0.3s; }";
  page += ".leaderboard { margin-top: 25px; background: #2c3e50; color: white; padding: 15px; border-radius: 8px; text-align: left; }";
  page += ".leaderboard h3 { margin-top: 0; text-align: center; color: #f1c40f; }";
  page += ".rank { display: flex; justify-content: space-between; font-size: 1.1rem; margin-bottom: 8px; border-bottom: 1px solid #34495e; padding-bottom: 5px; }";
  page += "</style>";
  
  // The "Remember Me" Javascript
  page += "<script>window.onload=function(){var n=localStorage.getItem('cName');if(n)document.getElementById('cName').value=n;}; function saveName(){localStorage.setItem('cName',document.getElementById('cName').value);}</script>";
  page += "</head><body>";
  
  page += "<div class='card'>";
  page += "<h2>Hangboard Control</h2>";
  
  // Notice the onsubmit tag added here to trigger the save
  page += "<form action='/set' method='GET' onsubmit='saveName()'>";
  
  page += "<label>Climber Name</label>";
  page += "<input type='text' id='cName' name='climberName' placeholder='Enter name' maxlength='10'>";
  
  page += "<label>Select Activity</label>";
  page += "<select name='mode'>";
  page += "<option value='Warmup' " + getSelected(currentMode, "Warmup") + ">Warmup (10s/5s x 3)</option>";
  page += "<option value='Endurance' " + getSelected(currentMode, "Endurance") + ">Endurance (20s/10s x 4)</option>";
  page += "<option value='MaxHang' " + getSelected(currentMode, "MaxHang") + ">Max Hang (Stopwatch to Failure)</option>"; 
  page += "<option value='Custom' " + getSelected(currentMode, "Custom") + ">--- Custom Builder (Coming Soon) ---</option>"; 
  page += "<option value='Weigh' " + getSelected(currentMode, "Weigh") + ">Scale: Measure Body Weight</option>"; 
  page += "</select>";

  page += "<label>Target Holds</label>";
  page += "<select name='holds'>";
  page += "<optgroup label='Top Row (35mm)'><option value='Top_All' " + getSelected(currentHolds, "Top_All") + ">Full Top Row</option><option value='Top_Outer' " + getSelected(currentHolds, "Top_Outer") + ">Outer Pockets</option><option value='Top_Inner' " + getSelected(currentHolds, "Top_Inner") + ">Inner Pockets</option><option value='Top_Center' " + getSelected(currentHolds, "Top_Center") + ">Center Jug</option></optgroup>";
  page += "<optgroup label='Bottom Row (20mm)'><option value='Bottom_All' " + getSelected(currentHolds, "Bottom_All") + ">Full Bottom Row</option><option value='Bottom_Outer' " + getSelected(currentHolds, "Bottom_Outer") + ">Outer Edges</option><option value='Bottom_Mid' " + getSelected(currentHolds, "Bottom_Mid") + ">Middle Edges</option><option value='Bottom_Inner' " + getSelected(currentHolds, "Bottom_Inner") + ">Inner Edges</option></optgroup>";
  page += "</select>";
  
  page += "<input type='submit' class='btn' value='START'>";
  page += "</form>";

  // --- Dynamic EEPROM Leaderboard Loop ---
  page += "<div class='leaderboard'><h3>🏆 TOP 5 MAX HANGS 🏆</h3>";
  for(int i = 0; i < NUM_LEADERBOARD; i++) {
    if(leaderboard[i].time > 0) {
      int m = leaderboard[i].time / 60;
      int s = leaderboard[i].time % 60;
      page += "<div class='rank'><span>" + String(i+1) + ". " + String(leaderboard[i].name) + "</span><span>" + String(m) + "m " + String(s) + "s</span></div>";
    } else {
      page += "<div class='rank'><span>" + String(i+1) + ". ---</span><span>0m 0s</span></div>";
    }
  }
  page += "</div></div></body></html>";

  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.send(200, "text/html", page);
}

// --- HANDLE WEB FORM SUBMISSION ---
void handleSelection() {
  if (server.hasArg("mode") && server.hasArg("holds")) {
    currentMode = server.arg("mode");
    currentHolds = server.arg("holds");

    // Capture user info
    if (server.hasArg("climberName")) {
      String inputName = server.arg("climberName");
      if (inputName != "") currentUser = inputName; 
    }

    // Fix for the Scale Bug: Take a snapshot of the current dead-weight
    if (currentMode == "Weigh") {
      baselineWeight = (LoadCell_1.getData() + LoadCell_2.getData()) / 1000.0;
    }
    
    // Set Timers based on Mode
    if (currentMode == "Warmup") {
      hangDuration = 10; restDuration = 5; totalSets = 3;
    } else if (currentMode == "Endurance") {
      hangDuration = 20; restDuration = 10; totalSets = 4;
    } else if (currentMode == "MaxHang") {
      hangDuration = 30; restDuration = 20; totalSets = 2;
    }

    currentSet = 1;
    currentState = PREP; 
    stateStartTime = millis();
    lcd.clear(); 
    
    // Flash target holds white to confirm selection
    lightUpHolds(CRGB::White, CRGB::Black);
    beep(200); 

    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", ""); 
  }
}

void setup() {
  Serial.begin(115200);

  EEPROM.begin(512);

  EEPROM.get(EEPROM_LEADERBOARD_START, leaderboard);
  
  // If memory is completely corrupt or fresh, initialize it with blanks
  if (leaderboard[0].time < 0 || leaderboard[0].time > 100000) {
    for (int i = 0; i < NUM_LEADERBOARD; i++) {
      String("Empty").toCharArray(leaderboard[i].name, 11);
      leaderboard[i].time = 0;
    }
    EEPROM.put(EEPROM_LEADERBOARD_START, leaderboard);
    #if defined(ESP8266) || defined(ESP32)
    EEPROM.commit();
    #endif
  }

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  FastLED.setBrightness(BRIGHTNESS);
  
  // Boot Sweep
  for(int i = 0; i < NUM_LEDS; i++) {
    leds[i] = CRGB::Blue;
    FastLED.show();
    delay(40); 
  }
  FastLED.clear(true); 

  Wire.begin(D2, D1);
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Taring scales...");

  LoadCell_1.begin();
  LoadCell_2.begin();
  delay(500);
  LoadCell_1.start(2000, true);
  LoadCell_2.start(2000, true);
  
  // Your exact calibration factors
  LoadCell_1.setCalFactor(-44.05);  
  LoadCell_2.setCalFactor(-42.58);  

  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Starting Hotspot");

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(apSSID, apPass); 
  delay(500);

  dnsServer.start(DNS_PORT, "*", apIP);
  server.on("/", handleRoot);
  server.on("/set", handleSelection);
  server.onNotFound([]() {
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
  });

  server.begin();
  
  beep(600); 
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("System Ready");
  lcd.setCursor(0,1);
  lcd.print("Scan QR to join");
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
  
  LoadCell_1.update();
  LoadCell_2.update();

  unsigned long currentMillis = millis();

  switch (currentState) {
    
    case IDLE:{
      if (currentMillis - ledUpdateTimer >= 20) {
        rainbowHue++; 
        fill_rainbow(leds, NUM_LEDS, rainbowHue, 7); 
        FastLED.show();
        ledUpdateTimer = currentMillis;
      }
      break;
    }
    case PREP:{
      if (currentMillis - displayUpdateTimer >= 250) {
        int secondsLeft = 5 - ((currentMillis - stateStartTime) / 1000);
        
        lcd.setCursor(0,0);
        lcd.print("Get Ready!      ");
        lcd.setCursor(0,1);
        lcd.print("Starting in: ");
        lcd.print(secondsLeft);
        lcd.print(" ");
        
        // Coach user: Highlight target holds in YELLOW, dim the rest
        lightUpHolds(CRGB::Yellow, CRGB(10, 10, 10));

        displayUpdateTimer = currentMillis;

       if (secondsLeft <= 0) {
          stateStartTime = currentMillis;
          lcd.clear();
          beep(600);
          
          if (currentMode == "Weigh") {
            currentState = WEIGH_WAIT;
            weightSum = 0; weightSamples = 0;
          } 
          else if (currentMode == "MaxHang") {
            currentState = MAX_HANG_ACTIVE; // Route to the new stopwatch
            maxHangSeconds = 0;
          } 
          else {
            currentState = HANG;
          }
        }
      }
      break;
    }
    case HANG:{
      if (currentMillis - displayUpdateTimer >= 250) {
        int secondsLeft = hangDuration - ((currentMillis - stateStartTime) / 1000);
        
        float w1_kg = LoadCell_1.getData() / 1000.0;
        float w2_kg = LoadCell_2.getData() / 1000.0;
        float total_kg = w1_kg + w2_kg;

        // Fail-Safe: Drop below 1.0kg after 1.5s grace period
        if ((currentMillis - stateStartTime > 1500) && (total_kg < 1.0)) {
          currentState = FAILED;
          stateStartTime = currentMillis;
          lcd.clear();
          beep(1000); 
          break; 
        }

        lcd.setCursor(0,0);
        lcd.print("HANG! S:");
        lcd.print(currentSet);
        lcd.print("/");
        lcd.print(totalSets);
        lcd.print(" "); 
        lcd.print(secondsLeft);
        lcd.print("s  ");
        
        lcd.setCursor(0,1);
        lcd.print("L:");
        lcd.print(w1_kg, 1); 
        lcd.print(" R:");
        lcd.print(w2_kg, 1);
        lcd.print(" "); 
        
        // Active Hang: Highlight target holds in RED, dim the rest
        lightUpHolds(CRGB::Red, CRGB(20, 0, 0));

        displayUpdateTimer = currentMillis;

        if (secondsLeft <= 0) {
          if (currentSet >= totalSets) {
            currentState = FINISHED;
          } else {
            currentState = REST;
          }
          stateStartTime = currentMillis;
          lcd.clear();
          beep(150); delay(100); beep(150);
        }
      }
      break;
    }
    case REST:{
      if (currentMillis - displayUpdateTimer >= 250) {
        int secondsLeft = restDuration - ((currentMillis - stateStartTime) / 1000);
        
        lcd.setCursor(0,0);
        lcd.print("REST!           ");
        lcd.setCursor(0,1);
        lcd.print("Next in: ");
        lcd.print(secondsLeft);
        lcd.print("s  ");
        
        // Solid dim blue for whole board during rest
        fill_solid(leds, NUM_LEDS, CRGB(0, 0, 40)); 
        FastLED.show();

        displayUpdateTimer = currentMillis;

        if (secondsLeft <= 0) {
          currentSet++;
          currentState = HANG;
          stateStartTime = currentMillis;
          lcd.clear();
          beep(600);
        }
      }
      break;
    }
    case WEIGH_WAIT:{
          if (currentMillis - displayUpdateTimer >= 250) {
            float total_kg = (LoadCell_1.getData() + LoadCell_2.getData()) / 1000.0;

            lcd.setCursor(0,0); lcd.print("Step on / Hang  ");
            lcd.setCursor(0,1); lcd.print("Waiting...      ");
            
            lightUpHolds(CRGB::Cyan, CRGB(5, 5, 5));
            displayUpdateTimer = currentMillis;

            // FIXED: Trigger only when weight jumps 10kg above the baseline snapshot!
            if (total_kg > (baselineWeight + 1.0)) {
              currentState = WEIGH_ACTIVE;
              stateStartTime = currentMillis;
              beep(400);
            }
          }
          break;
    }
    case MAX_HANG_ACTIVE:{
      if (currentMillis - displayUpdateTimer >= 250) {
            // Calculate total elapsed seconds
            unsigned long elapsed = currentMillis - stateStartTime;
            maxHangSeconds = elapsed / 1000;
            
            float w1_kg = LoadCell_1.getData() / 1000.0;
            float w2_kg = LoadCell_2.getData() / 1000.0;
            float total_kg = w1_kg + w2_kg;

      // Exit Condition: Fail if dropped below 1.0kg after 1.5s grace period
      if (elapsed > 1500 && total_kg < 1.0) {
          
          // --- NEW: SAVE TO LEADERBOARD ---
        if (maxHangSeconds > 0) { 
            updateLeaderboard(currentUser, maxHangSeconds); 
          }
          
          currentState = FINISHED; // Route directly to FINISHED to see score
          stateStartTime = currentMillis;
          lcd.clear();
          beep(1000); 
        break;
        }
      break;
     }
    }
    case WEIGH_ACTIVE:{
      // Run this very fast (every 50ms) to gather tons of samples
      if (currentMillis - displayUpdateTimer >= 50) {
        float total_kg = (LoadCell_1.getData() + LoadCell_2.getData()) / 1000.0;

        lcd.setCursor(0,0);
        lcd.print("Measuring...    ");
        lcd.setCursor(0,1);
        lcd.print("Hold still!     ");
        
        lightUpHolds(CRGB::Purple, CRGB(5, 5, 5));

        // Skip the first 1.5 seconds to let the physical swinging stop
        if (currentMillis - stateStartTime > 1500) {
          weightSum += total_kg;
          weightSamples++;
        }

        // After 4.5 seconds total (1.5s stabilize + 3.0s gathering data), finish
        if (currentMillis - stateStartTime > 4500) {
          finalWeight = weightSum / (float)weightSamples;
          currentState = WEIGH_DONE;
          stateStartTime = currentMillis;
          beep(800);
        }
        displayUpdateTimer = currentMillis;
      }
      break;
    }
    case WEIGH_DONE:{
      if (currentMillis - displayUpdateTimer >= 250) {
        lcd.setCursor(0,0);
        lcd.print("Final Weight:   ");
        lcd.setCursor(0,1);
        lcd.print(finalWeight, 2); // The '2' forces exactly 0.00 precision!
        lcd.print(" kg      ");
        
        // Pulse LEDs Green
        fill_solid(leds, NUM_LEDS, CRGB::Green);
        FastLED.show();

        displayUpdateTimer = currentMillis;

        // Keep the result on the screen for 8 seconds, then go back to idle
        if (currentMillis - stateStartTime > 8000) {
          currentState = IDLE;
          lcd.clear();
          lcd.setCursor(0,0);
          lcd.print("System Ready");
        }
      }
      break;
    }
    case FAILED:{
      if (currentMillis - displayUpdateTimer >= 250) {
        lcd.setCursor(0,0);
        lcd.print("Workout Failed! ");
        lcd.setCursor(0,1);
        lcd.print("Dropped early.  ");
        
        fill_solid(leds, NUM_LEDS, CRGB::Red);
        FastLED.show();
        
        displayUpdateTimer = currentMillis;

        if (currentMillis - stateStartTime > 3000) {
          currentState = IDLE;
          lcd.clear();
          lcd.setCursor(0,0);
          lcd.print("System Ready");
        }
      }
      break;
    }
    case FINISHED:{
      if (currentMillis - displayUpdateTimer >= 250) {
        lcd.setCursor(0,0);
        
        // --- Custom Display for Max Hang ---
        if (currentMode == "MaxHang") {
          lcd.print("Max Time:       ");
          lcd.setCursor(0,1);
          int m = maxHangSeconds / 60;
          int s = maxHangSeconds % 60;
          char timeStr[16];
          sprintf(timeStr, "%02dm %02ds        ", m, s);
          lcd.print(timeStr);
        } else {
          // Standard display for regular workouts
          lcd.print("Workout Complete");
          lcd.setCursor(0,1);
          lcd.print("Great job!      ");
        }
        
        fill_solid(leds, NUM_LEDS, CRGB::Green);
        FastLED.show();
        
        if (currentMillis - stateStartTime < 250) { beep(800); }

        displayUpdateTimer = currentMillis;

        // Extended view time to 6 seconds so you can read your score
        if (currentMillis - stateStartTime > 6000) {
          currentState = IDLE;
          lcd.clear();
          lcd.setCursor(0,0);
          lcd.print("System Ready");
        }
      }
      break;
    }
  }
}