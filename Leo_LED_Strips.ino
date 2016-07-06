// ESP8266 driver code for WS2812B LED strips.
// Connect LED strips to pin "D1" on NodeMCU board (this is GPIO 5)

#include <Adafruit_NeoPixel.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>
#include "FS.h"

// function declarations due to bug in Arduino IDE
bool checkPattern(); 
int gammaFix(int c);


#define PIN 5      // The pin the LED strip is connected to. GPIO5 on the NodeMCU board is labeled D1.
#define NUM_LEDS 30 // The number of LEDs in the strip.

const char* ssid = "Hacklab-Guests"; // The name of the Wifi-network to connect to, exactly, case sensitive.
const char* password = "";          // The password of the wifi network. Blank if no password.

int BOTTOM_INDEX = 0;
int TOP_INDEX = int(NUM_LEDS/2);
int EVENODD = NUM_LEDS%2;
int colorMode = -1;
int patternMode = 0;
int direction = 0;
int autoPattern = 0;
int CURR_PATTERN; // The pattern being changed.
long lastAuto = 0;
#define MAX_PATTERN  4 // The highest pattern in the land for auto-pattern

byte PatternChanged = 0; // When this turns to 1, it means exit the loop.

ESP8266WebServer server(80);
 
// Parameter 1 = number of pixels in strip
// Parameter 2 = pin number (most are valid)
// Parameter 3 = pixel type flags, add together as needed:
//   NEO_KHZ800  800 KHz bitstream (most NeoPixel products w/WS2812 LEDs)
//   NEO_KHZ400  400 KHz (classic 'v1' (not v2) FLORA pixels, WS2811 drivers)
//   NEO_GRB     Pixels are wired for GRB bitstream (most NeoPixel products)
//   NEO_RGB     Pixels are wired for RGB bitstream (v1 FLORA pixels, not v2)
Adafruit_NeoPixel leds = Adafruit_NeoPixel(NUM_LEDS, PIN, NEO_GRB + NEO_KHZ800);

// FUNKBOXING STUFF
//-PERISTENT VARS
int idex = 0;        //-LED INDEX (0 to NUM_LEDS-1
int idx_offset = 0;  //-OFFSET INDEX (BOTTOM LED TO ZERO WHEN LOOP IS TURNED/DOESN'T REALLY WORK)
int ihue = 0;        //-HUE (0-360)
int ibright = 0;     //-BRIGHTNESS (0-255)
int isat = 0;        //-SATURATION (0-255)
int bouncedirection = 0;  //-SWITCH FOR COLOR BOUNCE (0-1)
float tcount = 0.0;      //-INC VAR FOR SIN LOOPS
int lcount = 0;      //-ANOTHER COUNTING VAR
 
uint32_t makerCol[11]; 
uint32_t num = 0;

bool loadConfig() {
  File configFile = SPIFFS.open("/config.json", "r");
  if (!configFile) {
    Serial.println("Failed to open config file");
    return false;
  }

  size_t size = configFile.size();
  if (size > 1024) {
    Serial.println("Config file size is too large");
    return false;
  }

  // Allocate a buffer to store contents of the file.
  std::unique_ptr<char[]> buf(new char[size]);

  // We don't use String here because ArduinoJson library requires the input
  // buffer to be mutable. If you don't use ArduinoJson, you may as well
  // use configFile.readString instead.
  configFile.readBytes(buf.get(), size);

  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& json = jsonBuffer.parseObject(buf.get());

  if (!json.success()) {
    Serial.println("Failed to parse config file");
    return false;
  }

  colorMode = atoi(json["colorMode"]);
  patternMode = atoi(json["patternMode"]);
  direction = atoi(json["direction"]);

  return true;
}

bool saveConfig() 
{
  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& json = jsonBuffer.createObject();
  json["colorMode"] = colorMode;
  json["patternMode"] = patternMode;
  json["direction"] = direction;

  File configFile = SPIFFS.open("/config.json", "w");
  if (!configFile) {
    Serial.println("Failed to open config file for writing");
    return false;
  }

  json.printTo(configFile);
  return true;
}



void handleRoot() {
  String message = "LED Control:<BR><BR>";

  message += "<B>Current Colormode: ";
  message += colorMode;
  message += "</B><BR>";
  
  message += "<LI><A HREF=\"/set?color=red\">Red</A>";
  message += "<LI><A HREF=\"/set?color=green\">Green</A>";
  message += "<LI><A HREF=\"/set?color=blue\">Blue</A>";
  message += "<LI><A HREF=\"/set?color=white\">White</A>";
  message += "<LI><A HREF=\"/set?color=orange\">Orange</A>";
  message += "<LI><A HREF=\"/set?color=yellow\">Yellow</A>";
  message += "<LI><A HREF=\"/set?color=fuschia\">Fuschia</A>";
  message += "<LI><A HREF=\"/set?color=rainbow\">Rainbow</A>";

  message += "<BR><BR>Pattern: ";
  message += patternMode;
  message += "<BR>";
  message += "<LI><A HREF=\"/set?pattern=auto\">Auto</A>";
  message += "<LI><A HREF=\"/set?pattern=chase\">Chase</A>";
  message += "<LI><A HREF=\"/set?pattern=rainbow\">Rainbow</A>";
  message += "<LI><A HREF=\"/set?pattern=fadechase\">Fade Chase</A>";
  message += "<LI><A HREF=\"/set?pattern=fastlong\">Fast Long Chase</A>";
  
  message += "<LI><A HREF=\"/set?pattern=randomburst\">Random Burst</A>";
  message += "<LI><A HREF=\"/set?pattern=colorbounce\">Color Bounce</A>";
  message += "<LI><A HREF=\"/set?pattern=police1\">Police 1</A>";
  message += "<LI><A HREF=\"/set?pattern=police2\">Police 2</A>";
  message += "<LI><A HREF=\"/set?pattern=bouncefade\">Bounce Fade</A>";
  message += "<LI><A HREF=\"/set?pattern=flame\">Flame</A>";
  // message += "<LI><A HREF=\"/set?pattern=radiation\">Radiation</A>";

  
  message += "<BR><BR>Direction:<BR>";
  message += "<LI><A HREF=\"/set?direction=left\">Left</A>";
  message += "<LI><A HREF=\"/set?direction=right\">Right</A>";

  
  server.send(200, "text/html", message);
}



void handleSet() {
  // Handle changing the colors of the LED from the web server
  
  for (uint8_t i=0; i<server.args(); i++)
  {
    if (server.argName(i) == "color") 
    {
      if (server.arg(i) == "red") colorMode = 0;
      if (server.arg(i) == "green") colorMode = 4;
      if (server.arg(i) == "blue") colorMode = 9;
      if (server.arg(i) == "white") colorMode = 8;
      if (server.arg(i) == "orange") colorMode = 6;
      if (server.arg(i) == "yellow") colorMode = 5;
      if (server.arg(i) == "fuschia") colorMode = 7;
      if (server.arg(i) == "rainbow") colorMode = -1;
    }
    if (server.argName(i) == "pattern") 
    {
      if (server.arg(i) == "chase") patternMode = 0;
      if (server.arg(i) == "rainbow") patternMode = 1;
      if (server.arg(i) == "fadechase") patternMode = 2;
      if (server.arg(i) == "fastlong") patternMode = 3;

      if (server.arg(i) == "randomburst") patternMode = 4;
      if (server.arg(i) == "colorbounce") patternMode = 5;
      if (server.arg(i) == "police1") patternMode = 6;
      if (server.arg(i) == "police2") patternMode = 7;
      if (server.arg(i) == "bouncefade") patternMode = 8;
      if (server.arg(i) == "flame") patternMode = 9;
      if (server.arg(i) == "radiation") patternMode = 10;
      
      if (server.arg(i) == "auto") patternMode = 99; // Auto will automatically change the pattern every 10 seconds
      PatternChanged = 1;
    }
    if (server.argName(i) == "direction") 
    {
      if (server.arg(i) == "left") direction = 0;
      if (server.arg(i) == "right") direction = 1;
    }
  }

  saveConfig();
  handleRoot();
}







void handleNotFound(){
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET)?"GET":"POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i=0; i<server.args(); i++){
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}

void fadeChase()
{

  int n = 0;
  int c;
  int tt;
  
  for (int t = 0; t < 6000; t++)
  {
    tt = t;
    if (direction) tt = 6000 - t;

    for(uint16_t i=0; i <= leds.numPixels(); i++) {

        switch (colorMode)
        {
          case -1: c = makerCol[(((i+tt)/6)) % 10]; break;
          default: c = makerCol[colorMode];
        }
        
        
        float p = (float)(sin((float)(i + tt) / (float)5) + 1) / (float)2;
        
        c = ((byte)((float)((c & 0xFF0000) >> 16) * p) << 16) |
            ((byte)((float)((c & 0x00FF00) >> 8) * p) << 8) |
            ((byte)((float)(c & 0x0000FF) * p));


        
        leds.setPixelColor(i, c);
    }
    leds.show();
    delay(10); 
    server.handleClient();
    if (checkPattern()) return; // Break out of this loop if the pattern has been changed.
  }
  
}


void makerMarch()
{

  int n = 0;
  int c;
  int tt;
  
  for (int t = 0; t < 6000; t++)
  {
    tt = t;
    if (direction) tt = 6000 - t;
    
    for(uint16_t i=0; i <= leds.numPixels(); i++) {

        switch (colorMode)
        {
          case -1: c = makerCol[(((i+tt)/6)) % 10]; break;
          default: c = makerCol[colorMode];
        }
        
        leds.setPixelColor(i, (i + tt) % 6 == 0 ? c : 0);
    }
    leds.show();
    delay(70); 
    server.handleClient();
    if (checkPattern()) return; // Break out of this loop if the pattern has been changed.
  }
}

void FastLong()
{
  int n = 0;
  int c;
  int tt;
  for (int t = 0; t < 6000; t++)
  {
    tt = t;
    if (direction) tt = 6000 - t;
    
    for(uint16_t i=0; i <= leds.numPixels(); i++) {
        switch (colorMode)
        {
          case -1: c = makerCol[(((i+tt)/6)) % 10]; break;
          default: c = makerCol[colorMode];
        }
        leds.setPixelColor(i, (i + tt) % 35 == 0 ? c : 0);
    }
    leds.show();
    delay(10); 
    server.handleClient();
    if (checkPattern()) return; // Break out of this loop if the pattern has been changed.
  }
}

 
// Input a value 0 to 255 to get a color value.
// The colours are a transition r - g - b - back to r.
uint32_t Wheel(byte WheelPos) {
  if(WheelPos < 85) {
   return leds.Color(WheelPos * 3, 255 - WheelPos * 3, 0);
  } else if(WheelPos < 170) {
   WheelPos -= 85;
   return leds.Color(255 - WheelPos * 3, 0, WheelPos * 3);
  } else {
   WheelPos -= 170;
   return leds.Color(0, WheelPos * 3, 255 - WheelPos * 3);
  }
}

void rainbowCycle(uint8_t wait) {
  uint16_t i, j;
 
  for(j=0; j<256*5; j++) { // 5 cycles of all colors on wheel
    for(i=0; i< leds.numPixels(); i++) {
      leds.setPixelColor(i, Wheel(((i * 256 / leds.numPixels()) + j) & 255));
    }
    
    leds.show();
    delay(wait);
    server.handleClient();
    if (checkPattern()) return; // Break out of this loop if the pattern has been changed.
  }
}


void setup() {

  Serial.begin(115200);

  leds.begin();
  leds.setBrightness(255); //adjust brightness here
  leds.show(); // Initialize all pixels to 'off'
  
  makerCol[0] = gammaFix(leds.Color(255, 0, 0));
  makerCol[1] = gammaFix(leds.Color(255, 0, 127));
  makerCol[2] = gammaFix(leds.Color(200, 0, 255));
  makerCol[3] = gammaFix(leds.Color(0, 255, 255));
  makerCol[4] = gammaFix(leds.Color(0, 255, 0));
  makerCol[5] = gammaFix(leds.Color(255, 150, 0));
  makerCol[6] = gammaFix(leds.Color(255, 75, 0));
  makerCol[7] = gammaFix(leds.Color(255, 0, 255));
  makerCol[8] = gammaFix(leds.Color(255, 255, 255));
  makerCol[9] = gammaFix(leds.Color(0, 0, 255));
  makerCol[10] = gammaFix(leds.Color(0, 0, 0));


  if (!SPIFFS.begin()) {
    Serial.println("Failed to mount file system");
    return;
  }

  if (!loadConfig()) {
    Serial.println("Failed to load config");
  } else {
    Serial.println("Config loaded");
  }

  WiFi.enableAP(false); // TUrn off the built-in access point
  WiFi.begin(ssid, password);
  Serial.println("");

  // Wait for connection
  // Comment out the next 4 lines if you don't care if the device has connected to wifi or not.
  // Otherwise, if the wifi is not available, it will sit here and wait until it is, and not animate.
  
   while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
   }
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());


  server.on("/", handleRoot);
  server.on("/set", handleSet);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("HTTP server started");
}



bool checkPattern() // Return true if the pattern has changed.
{
  CURR_PATTERN = patternMode;
  if (CURR_PATTERN == 99) CURR_PATTERN = autoPattern;
  if (millis() - lastAuto > 10000 || millis() < lastAuto)
  {
    lastAuto = millis();
    autoPattern = (autoPattern + 1) % (MAX_PATTERN + 1);
    return true;
  }
  
  if (PatternChanged) 
  {
    PatternChanged = 0;
    return true;
  }

  return false;
}
 
// Fill the dots one after the other with a color  
int gamma(int c) {
  // Correct the gamma of the display -- The display is already almost at max brightness by value 200~ or so, so we adjust this.
  return (int)(pow(c, 2) / 255);
}
int gammaFix(int c) {
  return (gamma((c & 0xFF0000) >> 16) << 16) |
        (gamma((c & 0x00FF00) >> 8) << 8) |
        (gamma(c & 0x0000FF));
}

// NEW FUNCTIONS FROM FUNKBOXING

//-CONVERT HSV VALUE TO RGB
void HSVtoRGB(int hue, int sat, int val, int colors[3]) {
  // hue: 0-359, sat: 0-255, val (lightness): 0-255
  int r, g, b, base;

  if (sat == 0) { // Achromatic color (gray).
    colors[0]=val;
    colors[1]=val;
    colors[2]=val;
  } else  {
    base = ((255 - sat) * val)>>8;
    switch(hue/60) {
      case 0:
        r = val;
        g = (((val-base)*hue)/60)+base;
        b = base;
        break;
      case 1:
        r = (((val-base)*(60-(hue%60)))/60)+base;
        g = val;
        b = base;
        break;
      case 2:
        r = base;
        g = val;
        b = (((val-base)*(hue%60))/60)+base;
        break;
      case 3:
        r = base;
        g = (((val-base)*(60-(hue%60)))/60)+base;
        b = val;
        break;
      case 4:
        r = (((val-base)*(hue%60))/60)+base;
        g = base;
        b = val;
        break;
      case 5:
        r = val;
        g = base;
        b = (((val-base)*(60-(hue%60)))/60)+base;
        break;
    }
    colors[0]=r;
    colors[1]=g;
    colors[2]=b;
  }
}

//-FIND INDEX OF HORIZONAL OPPOSITE LED
int horizontal_index(int i) {
  //-ONLY WORKS WITH INDEX < TOPINDEX
  if (i == BOTTOM_INDEX) {return BOTTOM_INDEX;}
  if (i == TOP_INDEX && EVENODD == 1) {return TOP_INDEX + 1;}
  if (i == TOP_INDEX && EVENODD == 0) {return TOP_INDEX;}
  return NUM_LEDS - i;  
}

//-FIND INDEX OF ANTIPODAL OPPOSITE LED
int antipodal_index(int i) {
  //int N2 = int(NUM_LEDS/2);
  int iN = i + TOP_INDEX;
  if (i >= TOP_INDEX) {iN = ( i + TOP_INDEX ) % NUM_LEDS; }
  return iN;
}


//-FIND ADJACENT INDEX CLOCKWISE
int adjacent_cw(int i) {
  int r;
  if (i < NUM_LEDS - 1) {r = i + 1;}
  else {r = 0;}
  return r;
}


//-FIND ADJACENT INDEX COUNTER-CLOCKWISE
int adjacent_ccw(int i) {
  int r;
  if (i > 0) {r = i - 1;}
  else {r = NUM_LEDS - 1;}
  return r;
}

void ledssetPixel(int i, byte r, byte g, byte b)
{
  leds.setPixelColor(i, leds.Color(r, g, b));
}

void random_burst(int idelay) { //-RANDOM INDEX/COLOR
  int icolor[3];  
  
  idex = random(0,NUM_LEDS);
  ihue = random(0,359);

  HSVtoRGB(ihue, 255, 255, icolor);
  ledssetPixel(idex, icolor[0], icolor[1], icolor[2]);
  leds.show();  
  delay(idelay);
}

void color_bounce(int idelay) { //-BOUNCE COLOR (SINGLE LED)
  if (bouncedirection == 0) {
    idex = idex + 1;
    if (idex == NUM_LEDS) {
      bouncedirection = 1;
      idex = idex - 1;
    }
  }
  if (bouncedirection == 1) {
    idex = idex - 1;
    if (idex == 0) {
      bouncedirection = 0;
    }
  }  
  for(int i = 0; i < NUM_LEDS; i++ ) {
    if (i == idex) {ledssetPixel(i, 255, 0, 0);}
    else {ledssetPixel(i, 0, 0, 0);}
  }
  leds.show();
  delay(idelay);
}


void police_lightsONE(int idelay) { //-POLICE LIGHTS (TWO COLOR SINGLE LED)
  idex++;
  if (idex >= NUM_LEDS) {idex = 0;}
  int idexR = idex;
  int idexB = antipodal_index(idexR);  
  for(int i = 0; i < NUM_LEDS; i++ ) {
    if (i == idexR) {ledssetPixel(i, 255, 0, 0);}
    else if (i == idexB) {ledssetPixel(i, 0, 0, 255);}    
    else {ledssetPixel(i, 0, 0, 0);}
  }
  leds.show();  
  delay(idelay);
}


void police_lightsALL(int idelay) { //-POLICE LIGHTS (TWO COLOR SOLID)
  idex++;
  if (idex >= NUM_LEDS) {idex = 0;}
  int idexR = idex;
  int idexB = antipodal_index(idexR);
  ledssetPixel(idexR, 255, 0, 0);
  ledssetPixel(idexB, 0, 0, 255);
  leds.show();  
  delay(idelay);
}


void color_bounceFADE(int idelay) { //-BOUNCE COLOR (SIMPLE MULTI-LED FADE)
  if (bouncedirection == 0) {
    idex = idex + 1;
    if (idex == NUM_LEDS) {
      bouncedirection = 1;
      idex = idex - 1;
    }
  }
  if (bouncedirection == 1) {
    idex = idex - 1;
    if (idex == 0) {
      bouncedirection = 0;
    }
  }
  int iL1 = adjacent_cw(idex);
  int iL2 = adjacent_cw(iL1);
  int iL3 = adjacent_cw(iL2);  
  int iR1 = adjacent_ccw(idex);
  int iR2 = adjacent_ccw(iR1);
  int iR3 = adjacent_ccw(iR2); 
  
  for(int i = 0; i < NUM_LEDS; i++ ) {
    if (i == idex) {ledssetPixel(i, 255, 0, 0);}
    else if (i == iL1) {ledssetPixel(i, 100, 0, 0);}
    else if (i == iL2) {ledssetPixel(i, 50, 0, 0);}
    else if (i == iL3) {ledssetPixel(i, 10, 0, 0);}        
    else if (i == iR1) {ledssetPixel(i, 100, 0, 0);}
    else if (i == iR2) {ledssetPixel(i, 50, 0, 0);}
    else if (i == iR3) {ledssetPixel(i, 10, 0, 0);}    
    else {ledssetPixel(i, 0, 0, 0);}
  }
 
  leds.show();
  delay(idelay);
}


void flicker(int thishue, int thissat) {
  int random_bright = random(0,255);
  int random_delay = random(10,100);
  int random_bool = random(0,random_bright);
  int thisColor[3];
  
  if (random_bool < 10) {
    HSVtoRGB(thishue, thissat, random_bright, thisColor);

    for(int i = 0 ; i < NUM_LEDS; i++ ) {
      ledssetPixel(i, thisColor[0], thisColor[1], thisColor[2]);
    }
    
    leds.show();    
    delay(random_delay);
  }
}

void flame() {
  int acolor[3];
  int idelay = random(0,10);
  
  float hmin = 0.1; float hmax = 45.0;
  float hdif = hmax-hmin;
  int randtemp = random(0,3);
  float hinc = (hdif/float(TOP_INDEX))+randtemp;

  int ahue = hmin;
  for(int i = 0; i < TOP_INDEX; i++ ) {
    
    ahue = ahue + hinc;

    HSVtoRGB(ahue, 255, 255, acolor);
    
//    leds[i].r = acolor[0]; leds[i].g = acolor[1]; leds[i].b = acolor[2]; 
    ledssetPixel(i, acolor[0], acolor[1], acolor[2]);

    int ih = horizontal_index(i);
//    leds[ih].r = acolor[0]; leds[ih].g = acolor[1]; leds[ih].b = acolor[2];
    ledssetPixel(ih, acolor[0], acolor[1], acolor[2]);

    
//    leds[TOP_INDEX].r = 255; leds[TOP_INDEX].g = 255; leds[TOP_INDEX].b = 255;
        ledssetPixel(TOP_INDEX, 255, 255, 255);

  
    leds.show();    
    delay(idelay);
  }
}


 
void loop() {
  checkPattern();
  switch (CURR_PATTERN)
  {
    case 0: makerMarch(); break;
    case 1: rainbowCycle(20); break;
    case 2: fadeChase(); break;
    case 3: FastLong(); break;

    // FUNKBOXING FUNCTIONS
    case 4: random_burst(2); break;
    case 5: color_bounce(5); break;
    case 6: police_lightsONE(5); break;
    case 7: police_lightsALL(5); break;
    case 8: color_bounceFADE(5); break;
  }
  server.handleClient();
}
 





