#include <Arduino.h>
#include <SPI.h>
#include <TinyGPS++.h>
#include <Wire.h>
#include "oled.h"
#include "LoRaCom.h"
#include "weather.h"

//Configure the display
#define OLED_SDA 4
#define OLED_SCL 15
#define OLED_RST 23
OLED display=OLED(OLED_SDA,OLED_SCL);

//GPS
HardwareSerial Serial2(2);
TinyGPSPlus gps;
char chr;
int latitude = 0;
int longitude = 0;
bool positionChanged = false;

//Time of the last time check
int lastTime = 0;
bool timeSynchronised = false;

//Time
int timeHour = 0;
int timeMinute = 0;
int timeSecond = 0;
int timeOffset = 0;
bool setTime = false;

//Weather
uint8_t *currentWeatherIcon;
String currentWeatherDescription;
int currentTemp;
int minTemp;
int maxTemp;
bool weatherAvailable = false;

int currentStep = 0;
int lastStep = -1;
int stepSecond = 0;
int lastTimeLoop = 0;

//Lora payload
uint8_t loraPayload[]={0x00,0x00,0x00,0x00,0x00};
uint8_t loraReceived[10];
bool processingReceivedData = false;
bool newDataAvailable = false;

void setup() {
  //Consol output
  Serial.begin(9600);
  delay(1000);
  
  Serial.println(F("Starting"));

  //Serial connection to GPS
  Serial2.begin(9600);
  delay(1000);
  
  //Initialice Display
  display.init();

  displayCenteredTextNormal("Loading..");

  //LoRa communication
  // LMIC init
  os_init();
  // Reset the MAC state. Session and pending data transfers will be discarded.
  LMIC_reset();
  
  displayCenteredTextNormal("Ready");
  delay(1000);
  clearDisplay();

  //Initial time
  lastTime = millis();

  //Start connecting
  do_send(&sendjob);

  currentWeatherIcon = weatherIcon800;
  currentWeatherDescription = weather800;
  currentTemp = 8;
  minTemp = -1;
  maxTemp = -10;
}

void loop() {
  os_runloop_once();

  readGPSData();
  
  if(!gps.location.isValid()){
    displayCenteredTextNormal("Searching for GPS...");
  } else {
    //When there is downlink data process it
    if(newDataAvailable){
      processingReceivedData = true;
      processReceivedData();
      processingReceivedData = false;
      newDataAvailable = false;
    }
    
    updateTime();
    
    if(positionChanged){
      sendRequestToWebService();
      positionChanged = false;
    }

    //Switch to the different screens
    int currentTime = millis();
    if((currentTime - lastTimeLoop) >= 1000){
      lastTimeLoop = currentTime;
      switch(currentStep){
        case 0:
          timeStep();        
          break;
        case 1:
          currentWeatherStep();
          break;
        case 2:
          currentWeatherMinMaxStep();
          break;
      }
    }
  }
}

//Handeles the lora events
void onEvent (ev_t ev) {
    Serial.print(os_getTime());
    Serial.print(": ");
    switch(ev) {
        case EV_SCAN_TIMEOUT:
            Serial.println(F("EV_SCAN_TIMEOUT"));
            break;
        case EV_BEACON_FOUND:
            Serial.println(F("EV_BEACON_FOUND"));
            break;
        case EV_BEACON_MISSED:
            Serial.println(F("EV_BEACON_MISSED"));
            break;
        case EV_BEACON_TRACKED:
            Serial.println(F("EV_BEACON_TRACKED"));
            break;
        case EV_JOINING:
            Serial.println(F("EV_JOINING"));
            break;
        case EV_JOINED:
            Serial.println(F("EV_JOINED"));

            // Disable link check validation (automatically enabled
            // during join, but not supported by TTN at this time).
            LMIC_setLinkCheckMode(0);
            break;
        case EV_RFU1:
            Serial.println(F("EV_RFU1"));
            break;
        case EV_JOIN_FAILED:
            Serial.println(F("EV_JOIN_FAILED"));
            break;
        case EV_REJOIN_FAILED:
            Serial.println(F("EV_REJOIN_FAILED"));
            break;
            break;
        case EV_TXCOMPLETE:
            Serial.println(F("EV_TXCOMPLETE (includes waiting for RX windows)"));
            if (LMIC.txrxFlags & TXRX_ACK)
              Serial.println(F("Received ack"));
            if (LMIC.dataLen) {
              Serial.print(F("Received "));
              Serial.print(LMIC.dataLen);
              Serial.println(F(" bytes of payload"));
              for (int i = 0; i < LMIC.dataLen; i++) {
                if (LMIC.frame[LMIC.dataBeg + i] < 0x10) {
                    Serial.print(F("0"));
                }
                Serial.print(LMIC.frame[LMIC.dataBeg + i], HEX);
                //Save if array is not accessed somewhere else
                if(!processingReceivedData){
                  loraReceived[i] = LMIC.frame[LMIC.dataBeg + i];
                  if(i == (LMIC.dataLen - 1)){
                    newDataAvailable = true;
                  }
                }
              }
              Serial.println();
            }
            // Schedule next transmission
            loraPayload[0] = 0x00; //0 = don't process
            os_setTimedCallback(&sendjob, os_getTime()+sec2osticks(TX_INTERVAL), do_send);
            break;
        case EV_LOST_TSYNC:
            Serial.println(F("EV_LOST_TSYNC"));
            break;
        case EV_RESET:
            Serial.println(F("EV_RESET"));
            break;
        case EV_RXCOMPLETE:
            // data received in ping slot
            Serial.println(F("EV_RXCOMPLETE"));
            break;
        case EV_LINK_DEAD:
            Serial.println(F("EV_LINK_DEAD"));
            break;
        case EV_LINK_ALIVE:
            Serial.println(F("EV_LINK_ALIVE"));
            break;
         default:
            Serial.println(F("Unknown event"));
            break;
    }
}

//schedule a uplink message 
void do_send(osjob_t* j){
    // Check if there is not a current TX/RX job running
    if (LMIC.opmode & OP_TXRXPEND) {
        Serial.println(F("OP_TXRXPEND, not sending"));
    } else {
        // Prepare upstream data transmission at the next possible time.
        LMIC_setTxData2(1, loraPayload, sizeof(loraPayload), 0);
        Serial.println(F("Packet queued"));
    }
}

void timeStep(){
  if(lastStep != currentStep){
    stepSecond = 0;
    lastStep = currentStep;
  } 
  
  displayTime();
  stepSecond++;
  //Show the time 4 seconds
  if(stepSecond == 5){
    currentStep++;
  }
}

void currentWeatherStep(){
  if(weatherAvailable){
    if(lastStep != currentStep){
      displayCurrentWeather(currentWeatherIcon, currentWeatherDescription, currentTemp);
      lastStep = currentStep;
      stepSecond = 0;
    } else {
      stepSecond++;
      //Show current weather 2 seconds
      if(stepSecond == 3){
        currentStep++;
      }
    }
  } else {
    currentStep = 0;
    lastStep = 1;
  }
}

void currentWeatherMinMaxStep(){
  if(lastStep != currentStep){
    minMaxTemperatur(minTemp, maxTemp);
    lastStep = currentStep;
    stepSecond = 0;
  } else {
    stepSecond++;
    //Show min, max temperature 2 seconds
    if(stepSecond == 3){
      currentStep = 0;
    }
  }
}

//Reads the received payload from the downlink
void processReceivedData(){
  currentTemp = (int)(byteToInteger(loraReceived, 0, 1) / 10);
  minTemp = (int)(byteToInteger(loraReceived, 2, 3) / 10);
  maxTemp = (int)(byteToInteger(loraReceived, 4, 5) / 10);
  int weatherCode = byteToInteger(loraReceived, 6, 7);
  int timeOffsetNew = byteToInteger(loraReceived, 8, 9);

  //Check if time needs to be set new
  if(timeOffset != timeOffsetNew){
    timeOffset = timeOffsetNew;
    setTime = true;
  }

  setWeatherIcon(weatherCode);
  setWeatherDescription(weatherCode);
  weatherAvailable = true;
}

//Sets the weather icon
void setWeatherIcon(int weatherCode){
  if(weatherCode >= 700 && weatherCode < 800){
    currentWeatherIcon = weatherIcon7XX;
  }
  if(weatherCode == 800){
    currentWeatherIcon = weatherIcon800;
  }
  if(weatherCode >= 801 && weatherCode < 804){
    currentWeatherIcon = weatherIcon801_to_803;
  }
  if(weatherCode == 804){
    currentWeatherIcon = weatherIcon804;
  }
  if(weatherCode >= 300 && weatherCode < 400){
    currentWeatherIcon = weatherIcon3XX;
  }
  if(weatherCode >= 500 && weatherCode < 600 && weatherCode != 502 && weatherCode != 521){
    currentWeatherIcon = weatherIcon5XX;
  }
  if(weatherCode == 502){
    currentWeatherIcon = weatherIcon502;
  }
  if(weatherCode == 521){
    currentWeatherIcon = weatherIcon521;
  }
  if(weatherCode == 600 || weatherCode == 610){
    currentWeatherIcon = weatherIcon600_610_621;
  }
  if(weatherCode == 601 || weatherCode == 602 || weatherCode == 622 || weatherCode == 623){
    currentWeatherIcon = weatherIcon601_602_622_623;
  }
  if(weatherCode == 621){
    currentWeatherIcon = weatherIcon621;
  }
  if(weatherCode == 611 || weatherCode == 612){
    currentWeatherIcon = weatherIcon611_612;
  }
  if(weatherCode == 200 || weatherCode == 201 || weatherCode == 202){
    currentWeatherIcon = weatherIcon200_201_202;
  }
  if(weatherCode == 230 || weatherCode == 231 || weatherCode == 232 || weatherCode == 233){
    currentWeatherIcon = weatherIcon230_231_232_233;
  }
}

//Sets the weather descripition
void setWeatherDescription(int weatherCode){
  switch(weatherCode){
    case 200:
      currentWeatherDescription = weather200;
      break;
    case 201:
      currentWeatherDescription = weather201;
      break;
    case 202:
      currentWeatherDescription = weather202;
      break;
    case 230:
      currentWeatherDescription = weather230;
      break;
    case 231:
      currentWeatherDescription = weather231;
      break;
    case 232:
      currentWeatherDescription = weather232;
      break;
    case 233:
      currentWeatherDescription = weather233;
      break;
    case 300:
      currentWeatherDescription = weather300;
      break;
    case 301:
      currentWeatherDescription = weather301;
      break;
    case 302:
      currentWeatherDescription = weather302;
      break;
    case 500:
      currentWeatherDescription = weather500;
      break;
    case 501:
      currentWeatherDescription = weather501;
      break;
    case 502:
      currentWeatherDescription = weather502;
      break;
    case 511:
      currentWeatherDescription = weather511;
      break;
    case 520:
      currentWeatherDescription = weather520;
      break;
    case 521:
      currentWeatherDescription = weather521;
      break;
    case 522:
      currentWeatherDescription = weather522;
      break;
    case 600:
      currentWeatherDescription = weather600;
      break;
    case 601:
      currentWeatherDescription = weather601;
      break;
    case 602:
      currentWeatherDescription = weather602;
      break;
    case 610:
      currentWeatherDescription = weather610;
      break;
    case 611:
      currentWeatherDescription = weather611;
      break;
    case 612:
      currentWeatherDescription = weather612;
      break;
    case 621:
      currentWeatherDescription = weather621;
      break;
    case 622:
      currentWeatherDescription = weather622;
      break;
    case 623:
      currentWeatherDescription = weather623;
      break;
    case 700:
      currentWeatherDescription = weather700;
      break;
    case 711:
      currentWeatherDescription = weather711;
      break;
    case 721:
      currentWeatherDescription = weather721;
      break;
    case 731:
      currentWeatherDescription = weather731;
      break;
    case 741:
      currentWeatherDescription = weather741;
      break;
    case 751:
      currentWeatherDescription = weather751;
      break;
    case 800:
      currentWeatherDescription = weather800;
      break;
    case 801:
      currentWeatherDescription = weather801;
      break;
    case 802:
      currentWeatherDescription = weather802;
      break;
    case 803:
      currentWeatherDescription = weather803;
      break;
    case 804:
      currentWeatherDescription = weather804;
      break;
    default:
      currentWeatherDescription = weather900;
      break;
    
  }
}

//Convertes the given byte of the payload to a integer
int byteToInteger(uint8_t data[], int byte0, int byte1){
    //Check if value is negative --> Bit 8 is set if negative
    bool valueNegative = (data[byte0] >> 7) == 1;
    
    //Set Bit 4 of byte 0 to 0
    data[byte0] = ((data[byte0] << 1) & 255) >> 1;
    
    //Calculate float value
    int theValue = ((data[byte0] << 8) | data[byte1]);
    
    //Set algebraic sign
    if(valueNegative){
      theValue=theValue*(-1);
    }
    return theValue;
}

//Double=12x16 pixels
void displayCenteredTextDouble(String text){
    char textChar[text.length()+1];
    text.toCharArray(textChar, sizeof(textChar));
    display.clear();
    display.draw_string((int)((128 - (text.length()*12))/2),8,textChar,OLED::DOUBLE_SIZE);
    display.display();
}

//Double=12x16 pixels
void displayCenteredTextDoubleCharArray(char* charArray, int numChars){
    display.clear();
    display.draw_string((int)((128 - (numChars*12))/2),8,charArray,OLED::DOUBLE_SIZE);
    display.display();
}

//Normal=6x8 pixels
void displayCenteredTextNormal(String text){
    char textChar[text.length()+1];
    text.toCharArray(textChar, sizeof(textChar));
    display.clear();
    display.draw_string((int)((128 - (text.length()*6))/2),12, textChar);
    display.display();
}

void clearDisplay(){
  display.clear();
  display.display();
}

//Displays the icon, temperature and description for the weather
void displayCurrentWeather(uint8_t* icon, String description, int currentTemp){ 
  display.clear();
  
  //Unit
  String temp ="°C";
  char tempChar[temp.length()+1];
  temp.toCharArray(tempChar, sizeof(tempChar));
  display.draw_string((int)(128 - (temp.length()*12)),4,tempChar,OLED::DOUBLE_SIZE);

  //Temperature
  int digitsTemp =digitsInteger(currentTemp);
  char tempC[digitsTemp];
  itoa(currentTemp,tempC,10);
  display.draw_string((int)(128 - ((digitsTemp + 3)*12)),4,tempC,OLED::DOUBLE_SIZE);
  
  //Weather description
  char descriptionChar[description.length()+1];
  description.toCharArray(descriptionChar, sizeof(descriptionChar));
  display.draw_string((int)((128 - (description.length()*6))/2),24, descriptionChar);

  //Icon
  display.draw_bitmapReverse(10,0,32,32,icon);
  
  display.display();
}

//Display the min and max temperature of the forecast
void minMaxTemperatur(int minTemp, int maxTemp){
  display.clear();
  
  displayTemperature(minTemp, 0, "Min:");
  
  displayTemperature(maxTemp, 16, "Max:");
}

//Displays the given text, temperature at the specified Y position of the screen
void displayTemperature(int temp, int posY, String text){
  //Text
  char textChar[text.length()+1];
  text.toCharArray(textChar, sizeof(textChar));
  display.draw_string(0,posY,textChar,OLED::DOUBLE_SIZE);

  //Unit
  String unit ="°C";
  char unitChar[unit.length()+1];
  unit.toCharArray(unitChar, sizeof(unitChar));
  display.draw_string((int)(128 - (unit.length()*12)),posY,unitChar,OLED::DOUBLE_SIZE);
  
  //Max temperature
  int digitsTemp =digitsInteger(temp);
  char tempC[digitsTemp];
  itoa(temp,tempC,10);
  display.draw_string((int)(128 - ((digitsTemp + 3)*12)),posY,tempC,OLED::DOUBLE_SIZE);
  
  display.display();
}

//Get the number of digits of an integer
int digitsInteger(int integerValue){
  int digits = 0;
  if(integerValue >= 0){
    if(integerValue >= 0 && integerValue < 10){
      digits = 1;
    } else {
      if(integerValue >= 10 && integerValue < 100){
        digits = 2;
      } else {
        digits = 3;
      }
    }
  } else {
    if(integerValue < 0 && integerValue > -10){
      digits = 2;
    } else {
      if(integerValue <= -10 && integerValue > -100){
        digits = 3;
      } else {
        digits = 4;
      }
    }
  }

  return digits;
}

void readGPSData(){
  while (Serial2.available() > 0) {
    //Check if serial communication is fuctional
    chr = Serial2.read();
    
    gps.encode(chr);
    
    // check if location data is updated
    if (gps.location.isValid() && gps.location.isUpdated())
    {
      int tmpLatitude = gps.location.lat() * 100;
      int tmpLongitude = gps.location.lng() * 100;
      //Update time and possition if the possition changed or the time needs to be set new
      if(tmpLatitude != latitude || tmpLongitude != longitude || setTime){
        timeHour = gps.time.hour();
        timeMinute = gps.time.minute();
        
        //Add time offset
        timeHour += timeOffset;
        //Check that time between 0 and 24
        if(timeHour < 0){
          timeHour = 23;
        }
        if(timeHour > 23){
          timeHour = 0;
        }
        setTime = false;
                
        latitude = tmpLatitude;
        longitude = tmpLongitude;
        
        Serial.print("latitude: ");
        Serial.println(latitude);

        Serial.print("longitude: ");
        Serial.println(longitude);
        
        Serial.print(timeHour);
        Serial.print(":");
        Serial.println(timeMinute);
        
        positionChanged = true;
      }
      //Update seconds always for synchronisation
      timeSecond = gps.time.second();
    }
  }
}

void updateTime(){
  int currentTime = millis();
  //Try to keep seconds as synchronised as possible
  if (gps.location.isValid() && !timeSynchronised){
    int currentSecond = (currentTime - lastTime);
    if(currentSecond < (timeSecond *1000)){
      //move the last time check backwards so the time will be updated earlier
      lastTime -= (timeSecond *1000) - currentSecond;
    }
    timeSynchronised = true;
  }
  
  if((currentTime - lastTime) >= 60000){
    //Check if we are currently in minute 59 of the hour
    if(timeMinute == 59){
      //If we are in hour 23 then set it back to 0 otherwise increase
      if(timeHour == 23){
        timeHour = 0;
      } else {
        timeHour += 1;
      }
      timeMinute = 0;
    } else {
      timeMinute += 1;
    }
    lastTime = currentTime;
  }
}

//Displays the current time in the format HH:mm
void displayTime(){
  display.clear();

  //Hour
  int digitsTime =digitsInteger(timeHour);
  char timeC[digitsTime];
  itoa(timeHour,timeC,10);
  if(digitsTime == 1){
    //If the hour is between 1 and 9 a 0 needs to be added manualy
    display.draw_string((int)(((128 - (digitsTime*12))/2) - 12),8,timeC,OLED::DOUBLE_SIZE);
    itoa(0,timeC,10);
    display.draw_string((int)(((128 - (digitsTime*12))/2) - 24),8,timeC,OLED::DOUBLE_SIZE);
  } else {
    display.draw_string((int)(((128 - (digitsTime*12))/2) - 20),8,timeC,OLED::DOUBLE_SIZE);
  }

  //seperator
  String text =":";
  char textChar[text.length()+1];
  text.toCharArray(textChar, sizeof(textChar));
  display.draw_string((int)((128 - (text.length()*12))/2),8,textChar,OLED::DOUBLE_SIZE);

  //Minute
  digitsTime =digitsInteger(timeMinute);
  timeC[digitsTime];
  itoa(timeMinute,timeC,10);
  if(digitsTime == 1){
    //If the minute is between 1 and 9 a 0 needs to be added manualy
    display.draw_string((int)(((128 - (digitsTime*12))/2) + 22),8,timeC,OLED::DOUBLE_SIZE);
    itoa(0,timeC,10);
    display.draw_string((int)(((128 - (digitsTime*12))/2) + 10),8,timeC,OLED::DOUBLE_SIZE);
  } else {
    display.draw_string((int)(((128 - (digitsTime*12))/2) + 18),8,timeC,OLED::DOUBLE_SIZE);
  }

  display.display();
}

//Generates the payload for the request to the web service and sends the array
void sendRequestToWebService(){
  loraPayload[0] = 0x01;//Set to action byte to 0x01 so it will be processed

  //Convert the latitued value to a byte array and store it in the payload array
  uint8_t latitudeBytes[2];
  toBytes(latitude, latitudeBytes);
  loraPayload[1] = latitudeBytes[0];
  loraPayload[2] = latitudeBytes[1];

  //Convert the longitued value to a byte array and store it in the payload array
  uint8_t longitudeBytes[2];
  toBytes(longitude, longitudeBytes);
  loraPayload[3] = *longitudeBytes;
  loraPayload[4] = *(longitudeBytes + 1);

  do_send(&sendjob);
}

//Converts an integer value to a byte array for the payload
void toBytes(int value, uint8_t bytes[]){
  bool negativeValue = false;

  //Check if value is negative and make it positive
  if(value < 0){
    negativeValue = true;
    value *= (-1);
  }

  //If the value is greater than 255 make a shift
  //bits to the right and store the bit 8 to 15 in byte[0]
  if(value > 255){
    bytes[0] = (value >> 8);
  }
  
  //Make all bits after bit 7 to 0 and connect it by using the bit & operation
  bytes[1] = (((~(value >> 8)) << 8) | 255) & value;
  
  if(negativeValue){
    bytes[0] = bytes[0] | 128;
  }
}
