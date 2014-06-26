#include <avr/wdt.h>
#include <LiquidCrystal.h>
#include <LCDKeypad.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <SPI.h>
#include <Ethernet.h>
#include <Timer.h>
#include <DHT.h>                // From https://github.com/adafruit/DHT-sensor-library
#include <CarrierHeatpumpIR.h>  // From HeatpumpIR library, https://github.com/ToniA/arduino-heatpumpir/archive/master.zip
#include "emoncmsApikey.h"      // This only defines the API key. Excluded from Git, for obvious reasons

#define FIREPLACE_FAN_PIN    49 // Pin for the fireplace relay
#define WATER_STOP_VALVE_PIN 29 // Pin for the Water stop relay
#define WAREHOUSE_RELAY_PIN  35 // Pin for the relay in the warehouse  (optional)

#define DHT11_PIN            39 // Pin for the DHT11 temperature/humidity sensor, uses +5, GND and some digital pin
#define MQ7_PIN             A15 // Pin for the MQ-7 CO sensor pin, uses +5V, GND and some analog pin
#define MG811_PIN           A14 // Pin for the MG-811 Co2 sensor pin, uses +5V, GND and some analog pin
#define IR_PIN               46 // Pin for the IR led, must be a PWM pin

#define ALARM_STATE_PIN      22 // Pin for the alarm system state

// Use digital pins 4, 5, 6, 7, 8, 9, 10, and analog pin 0 to interface with the LCD
// Do not use Pin 10 while this shield is connected
LCDKeypad lcd;

// DS18B20 sensor wire colors:
// * Red: Vcc
// * Yellow: Ground
// * Green: Data

// 1-wire buses

OneWire ow0(30);
DallasTemperature owsensors0(&ow0);
OneWire ow1(38);
DallasTemperature owsensors1(&ow1);
OneWire ow2(34);
DallasTemperature owsensors2(&ow2);
OneWire ow3(31);
DallasTemperature owsensors3(&ow3);
OneWire ow4(33);
DallasTemperature owsensors4(&ow4);
OneWire ow5(21);
DallasTemperature owsensors5(&ow5);
OneWire ow6(24);
DallasTemperature owsensors6(&ow6);
OneWire ow7(42);
DallasTemperature owsensors7(&ow7);
OneWire ow8(44);
DallasTemperature owsensors8(&ow8);
OneWire ow9(40);
DallasTemperature owsensors9(&ow9);
OneWire ow10(43);
DallasTemperature owsensors10(&ow10);
OneWire ow11(41);
DallasTemperature owsensors11(&ow11);
OneWire ow12(37);
DallasTemperature owsensors12(&ow12);
OneWire ow13(23);
DallasTemperature owsensors13(&ow13);
OneWire ow14(36);
DallasTemperature owsensors14(&ow14);
OneWire ow15(32);
DallasTemperature owsensors15(&ow15);
OneWire ow16(26);
DallasTemperature owsensors16(&ow16);
OneWire ow17(28);
DallasTemperature owsensors17(&ow17);

// Structure to hold them
typedef struct owbus owbus;
struct owbus
{
    DallasTemperature owbus;
    float temperature;
    char* emon_name;
    char* name;
};

// and the array
owbus owbuses[] = {
  {owsensors0, DEVICE_DISCONNECTED, "fireplace", "Takka huone"},             // Fireplace
  {owsensors1, DEVICE_DISCONNECTED, "kitchen", "Keitti\xEF"},                // Kitchen
  {owsensors2, DEVICE_DISCONNECTED, "utl_room", "Kodinhoitohuone"},          // Utility room
  {owsensors3, DEVICE_DISCONNECTED, "bedroom", "Julian huone"},              // Bedroom
  {owsensors4, DEVICE_DISCONNECTED, "master_bedroom", "Makuuhuone"},         // Master bedroom
  {owsensors5, DEVICE_DISCONNECTED, "warehouse", "Varasto"},                 // Warehouse
  {owsensors6, DEVICE_DISCONNECTED, "outdoor", "Ulkoilma"},                  // Outdoor air
  {owsensors7, DEVICE_DISCONNECTED, "aircond_intake", "Imuilma \x7E ILP "},  // Carrier intake air
  {owsensors8, DEVICE_DISCONNECTED, "aircond_out", "ILP \x7E puhallus"},     // Carrier outlet air
  {owsensors9, DEVICE_DISCONNECTED, "aircond_hotpipe", "ILP kuumakaasu"},    // Carrier hot gas pipe
  {owsensors10,DEVICE_DISCONNECTED, "boiler_mid", "Kuumavesivar.keski"},     // Hot water boiler middle
  {owsensors11,DEVICE_DISCONNECTED, "boiler_top", "Kuumavesivar.yl\xE1"},    // Hot water boiler up
  {owsensors12,DEVICE_DISCONNECTED, "hot_water", "Kuumavesi"},               // Hot water
  {owsensors13,DEVICE_DISCONNECTED, "water", "Tuleva vesi"},                 // Cold Water
  {owsensors14,DEVICE_DISCONNECTED, "vent_outdoor", "Ulkoilma \x7E LTO"},    // Ventilation machine fresh air in
  {owsensors15,DEVICE_DISCONNECTED, "vent_fresh", "LTO l\xE1mmitt\xE1\xE1"}, // Ventilation machine fresh air out
  {owsensors16,DEVICE_DISCONNECTED, "vent_dirty", "LTO \x7E sis\xE1ilma"},   // Ventilation machine waste air in
  {owsensors17,DEVICE_DISCONNECTED, "vent_waste", "LTO \x7E poistoilma"}     // Ventilation machine waste air out
};


// Structure for the Carrier mode
typedef struct CarrierHeatpump CarrierHeatpump;
struct CarrierHeatpump
{
  int operatingMode;
  int fanSpeed;
  int temperature;
  bool fireplaceFan;
};

// Default mode for the heatpump is HEAT, AUTO FAN and 22 degrees.
// 22 degrees also means that 5 minutes after startup the command will be
// sent if outdoor < 15 degrees
CarrierHeatpump carrierHeatpump = { 2, 0, 22, false };

// The Carrier heatpump instance, and the IRSender instance
HeatpumpIR *heatpumpIR = new CarrierHeatpumpIR();
IRSender irSender(IR_PIN); // IR led on Mega digital pin 46

// The number of the displayed sensor
int displayedSensor = 0;

// MAC & IP address for the Ethernet shield
byte macAddress[6] = { 0x02, 0x26, 0x89, 0x00, 0x00, 0xFE};
IPAddress ip(192, 168, 1, 9);

// The timers
Timer timer;

// The amount of water meter pulses since the last update to emoncms
volatile int waterPulses = 0;
int waterPulsesHistory[12] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}; // 12 minute water use history

// DHT11, MQ-7 & MG811 sensor readings
DHT dht(DHT11_PIN, DHT11);

float DHT11Humidity = 0.0;
float DHT11Temperature = 0.0;
int MQ7COLevel = 0;
float MG811CO2Level = 400;
float MG811Voltage = 0.0;

// Alarm state
int alarmState;
int alarmStateHistory;

// test
int test;

// Water state
bool waterState;
bool waterLeakState;
bool ShowerWaterUse;
unsigned long lastWaterPulse = 0;


void setup()
{
  // Serial initialization
  Serial.begin(9600);
  delay(500);
  Serial.println(F("Starting..."));

  // LCD initialization
  lcd.begin(16, 2);
  lcd.clear();
  lcd.print("K\xE1ynnistyy..."); // 'Starting...'
  delay(1000);

  // Fireplace fan relay
  pinMode(FIREPLACE_FAN_PIN, OUTPUT);

  // Default mode for the Fireplace is OFF
  digitalWrite(FIREPLACE_FAN_PIN, HIGH);  // Fireplace fan to OFF state

 // Water stop relay
  pinMode(WATER_STOP_VALVE_PIN, OUTPUT);

  // Default mode for the Water stop is OFF
  digitalWrite(WATER_STOP_VALVE_PIN, HIGH); // Water stop to OFF state, i.e. water flows

  // Relay in the warehouse
  pinMode(WAREHOUSE_RELAY_PIN, OUTPUT);

  // Default mode for the warehouse relay is OFF
  digitalWrite(WAREHOUSE_RELAY_PIN, HIGH); // Warehouse relay to OFF state

  // Alarm state is an input pin
  pinMode(ALARM_STATE_PIN, INPUT_PULLUP);

  // waterLeak state is false
  waterLeakState == false;
  ShowerWaterUse == false;

  // List OneWire devices
  for (int i=0; i < sizeof(owbuses) / sizeof(struct owbus); i++)
  {
    lcd.clear();
    lcd.print(owbuses[i].name);
    lcd.setCursor(0, 1);
    lcd.print("laitteita: "); // 'devices'

    Serial.print(F("VÃ¤ylÃ¤ssÃ¤ ")); // 'In bus'
    Serial.print(owbuses[i].name);
    Serial.print(F(" on ")); // 'there is'
    owbuses[i].owbus.begin();
    owbuses[i].owbus.setWaitForConversion(false);
    int deviceCount = owbuses[i].owbus.getDeviceCount();
    Serial.print(deviceCount);
    Serial.println(" laitetta"); // 'devices'
    lcd.print(deviceCount);
    lcd.print(" kpl");

    delay(1000);
  }

  Serial.println("Initializing Ethernet...");

  // initialize the Ethernet adapter with static IP address
  Ethernet.begin(macAddress, ip);

  delay(1000); // give the Ethernet shield a second to initialize

  lcd.clear();
  lcd.print("IP-osoite");
  lcd.setCursor(0, 1);
  lcd.print(Ethernet.localIP());

  Serial.print("IP address (static): ");
  Serial.println(Ethernet.localIP());

  // Temperatures to be measured immediately
  requestTemperatures();

  // The timed calls
  timer.every(2000, feedWatchdog);          // every 2 seconds
  timer.every(2000, updateDisplay);         // every 2 seconds
  timer.every(2000, alarmWaterShutoff);     // every 1 seconds
  timer.every(60000, readSensors);          // every minute
  timer.every(60000, updateEmoncms);        // every minute
  timer.every(60000, checkForWaterShutoff); // every minute
  timer.every(15000, requestTemperatures);  // every 15 seconds
  timer.every(330017L, controlCarrier);     // every ~5 minutes

  // Water meter pulse counter interrupt
  // interrupt 3 uses pin 20
  attachInterrupt(3, incrementWaterPulses, FALLING);

  // Initialize the DHT library
  dht.begin();

  // Enable watchdog
  wdt_enable(WDTO_8S);
}

void loop()
{
  timer.update();

  // Alarm state is not an interrupt
  alarmState = digitalRead(ALARM_STATE_PIN);
}

// Request the temperature measurement on all 1-wire buses
// and schedule an event 750ms later to read the measurements
void requestTemperatures()
{
  for (int i=0; i < sizeof(owbuses) / sizeof(struct owbus); i++) {
    owbuses[i].owbus.requestTemperatures();
  }

  timer.after(750, readTemperatures);
}

// Read the measured temperatures
void readTemperatures()
{
  for (int i=0; i < sizeof(owbuses) / sizeof(struct owbus); i++) {
    owbuses[i].temperature = owbuses[i].owbus.getTempCByIndex(0);
  }
}

// Update the LCD display
void updateDisplay()
{
  // First show the temperature displays
  if ( displayedSensor < sizeof(owbuses) / sizeof(struct owbus) &&
       owbuses[displayedSensor].temperature != DEVICE_DISCONNECTED) {
    // Display the device name and temperature on the LCD
    lcd.clear();
    lcd.print(owbuses[displayedSensor].name);
    lcd.setCursor(0, 1);
    lcd.print(owbuses[displayedSensor].temperature);
    lcd.print(" \xDF""C");

    displayedSensor++;
  } else if (displayedSensor < (sizeof(owbuses) / sizeof(struct owbus) + 1)) {
    // Ventilation machine waste air in sensor level
    lcd.clear();
    lcd.print("Sis\xE1ilma \x7E LTO");

    lcd.setCursor(0, 1);
    lcd.print(DHT11Temperature);
    lcd.print(" \xDF""C");

    displayedSensor++;
  } else if (displayedSensor < (sizeof(owbuses) / sizeof(struct owbus) + 2)) {
    // Humidity sensor level
    lcd.clear();
    lcd.print("Ilmankosteus");

    lcd.setCursor(0, 1);
    lcd.print(DHT11Humidity);
    lcd.print(" %  Sis\xE1ll\xE1");


    // And the same on debug display
    Serial.print(owbuses[displayedSensor].name);
    Serial.print(F(": "));
    Serial.println(owbuses[displayedSensor].temperature);
    displayedSensor++;
  } else if (displayedSensor < (sizeof(owbuses) / sizeof(struct owbus) + 3)) {
    // Heatpump temperature difference
    Serial.print("ILP lÃ¤mpenemÃ¤: ");
    Serial.println(owbuses[9].temperature - owbuses[8].temperature);

    lcd.clear();
    if (carrierHeatpump.operatingMode == MODE_HEAT)
    {
      lcd.print("ILP l\xE1mmitt\xE1\xE1:");
      lcd.setCursor(0, 1);
      lcd.print(owbuses[9].temperature - owbuses[8].temperature);
      lcd.print(" \xDF""C");
    } else if (carrierHeatpump.operatingMode == MODE_COOL) {
      lcd.print("ILP j\xE1\xE1hdytt\xE1\xE1:");
      lcd.setCursor(0, 1);
      lcd.print(owbuses[8].temperature - owbuses[9].temperature);
      lcd.print(" \xDF""C");
    } else {
      lcd.print("ILP puhaltaa:");
      lcd.setCursor(0, 1);
      lcd.print(owbuses[9].temperature);
      lcd.print(" \xDF""C");
    }
    displayedSensor++;
  } else if (displayedSensor < (sizeof(owbuses) / sizeof(struct owbus) + 4)) {
    // Mode display
    Serial.print("MODE: ");
    Serial.println(carrierHeatpump.operatingMode);
    Serial.print("FAN: ");
    Serial.println(carrierHeatpump.fanSpeed);
    Serial.print("TEMP: ");
    Serial.println(carrierHeatpump.temperature);

    lcd.clear();
    lcd.print("MODE: ");
    switch (carrierHeatpump.operatingMode) {
      case MODE_COOL:
        lcd.print("COOL");
        break;
      case MODE_HEAT:
        lcd.print("HEAT");
        break;
      case MODE_FAN:
        lcd.print("FAN");
        break;
    }

    lcd.setCursor(0, 1);
    lcd.print("TEMP: ");
    lcd.print(carrierHeatpump.temperature);

    lcd.setCursor(10, 1);
    lcd.print("FAN: ");
    if (carrierHeatpump.fanSpeed == FAN_AUTO) {
      lcd.print("A");
    } else {
      lcd.print(carrierHeatpump.fanSpeed);
    }
    displayedSensor++;
  } else if (displayedSensor < (sizeof(owbuses) / sizeof(struct owbus) + 5)) {
    // Fireplace mode display
    lcd.clear();
    lcd.print("Takkapuhallin");

    if (carrierHeatpump.fireplaceFan) {
      lcd.setCursor(0, 1);
      lcd.print("ON");
      Serial.println("Fireplace fan: ON");
    } else {
      lcd.setCursor(0, 1);
      lcd.print("OFF");
      Serial.println("Fireplace fan: OFF");
    }
    displayedSensor++;
  } else if (displayedSensor < (sizeof(owbuses) / sizeof(struct owbus) + 6)) {
    // CO2 level
    lcd.clear();
    lcd.print("CO-taso");

    lcd.setCursor(0, 1);
    lcd.print(MQ7COLevel);

    // lcd.print(" ppm"); // This is certainly not ppm's. I don't know what unit this number stands for

   displayedSensor++;
  } else if (displayedSensor < (sizeof(owbuses) / sizeof(struct owbus) + 7)) {
    // CO2 level
    lcd.clear();
    lcd.print("CO2-taso ");
    lcd.print(MG811Voltage);
    lcd.print(" V");
    lcd.setCursor(0, 1);
    lcd.print(MG811CO2Level);
    lcd.print(" ppm");

    displayedSensor++;
  } else if (displayedSensor < (sizeof(owbuses) / sizeof(struct owbus) + 8)) {
    // water state mode display
    lcd.clear();
    lcd.print("Vedensulku");
    lcd.setCursor(0, 1);
    if (waterState == LOW) {
      lcd.print("ON");
    } else {
      lcd.print("OFF");
    }

    displayedSensor++;
  } else if (displayedSensor < (sizeof(owbuses) / sizeof(struct owbus) + 9)) {
    // water Leak State mode display
    lcd.clear();
    lcd.print("Vuototesti");
    lcd.setCursor(0, 1);
    if (waterLeakState == true) {
      lcd.print("Vesivuoto");
    } else {
      lcd.print("OK");
    }

     displayedSensor++;
  } else if (displayedSensor < (sizeof(owbuses) / sizeof(struct owbus) + 10)) {
    // water Leak State mode display
    lcd.clear();
    lcd.print("Suikunk\xE1ytt\xEF");
    lcd.setCursor(0, 1);
    if (ShowerWaterUse == true) {
      lcd.print("yli 12 min");
    } else {
      lcd.print("OK");
    }

    displayedSensor++;
  } else if (displayedSensor < (sizeof(owbuses) / sizeof(struct owbus) + 11)) {
    // alarm State mode display
    lcd.clear();
    lcd.print("h\xE1lytin");
    lcd.setCursor(0, 1);
    if (alarmState == LOW) {
      lcd.print("ON");
    } else {
      lcd.print("OFF");
    }

    displayedSensor++;
  } else if (displayedSensor < (sizeof(owbuses) / sizeof(struct owbus) + 12)) {

    // waterPulsesHistory
    lcd.clear();
    lcd.print("Veden kulutus");
    lcd.setCursor(0, 1);
    lcd.print(waterPulses);
    lcd.print(" Litraa ");

    displayedSensor++;
  } else if (displayedSensor < (sizeof(owbuses) / sizeof(struct owbus) + 13)) {
    lcd.clear();
    lcd.print("Vedenk\xE1ytt\xEF");
    lcd.setCursor(0, 1);
    lcd.print("historia 12 min");

    displayedSensor++;
  } else if (displayedSensor < (sizeof(owbuses) / sizeof(struct owbus) + 14)) {

    lcd.clear();
    lcd.setCursor(0, 0);

    // Log the waterPulsesHistory[0-6] readings
    for (int i=0; i < sizeof(waterPulsesHistory) / sizeof(int)-6; i++) {
      lcd.print(waterPulsesHistory[i]);
      if (i < 5)
        lcd.print(",");
      else {
      lcd.print(" Lit");
      }
    }
    lcd.setCursor(0, 1);

    // Log the waterPulsesHistory[6-12] readings
    for (int i=6; i < sizeof(waterPulsesHistory) / sizeof(int); i++) {
      lcd.print(waterPulsesHistory[i]);
      if (i < 11)
        lcd.print(",");
      else {
      lcd.print(" raa");
      }
    }
    displayedSensor = 0;
  }
}
// Decide what to do with the Carrier heatpump

void controlCarrier()
{
  Serial.println("Controlling Carrier");

  int operatingMode = 2;
  int fanSpeed = 0;
  int temperature = 23;

  int aircond_intake = owbuses[7].temperature;
  int outdoor = owbuses[14].temperature;
  int fireplace = owbuses[0].temperature;
  int utility = owbuses[2].temperature;
  int kitchen = owbuses[1].temperature;

  // Fireplace fan control

  if (fireplace <= 24) {
    digitalWrite(FIREPLACE_FAN_PIN, HIGH);  // Fireplace fan to OFF state
    Serial.println("Takkapuhallin pois");
    carrierHeatpump.fireplaceFan = false;
  } else if (fireplace > 25) {
    digitalWrite(FIREPLACE_FAN_PIN, LOW);   // Fireplace fan to ON state
    Serial.println("Takkapuhallin pÃ¤Ã¤lle");
    carrierHeatpump.fireplaceFan = true;
  }

  // Heatpump control
  // Set the mode based on the outdoor temperature (summer cooling)
  if (outdoor >= 21 && aircond_intake >= 24) {
    operatingMode = MODE_COOL;
    temperature = 24;
    fanSpeed = FAN_AUTO;

  } else if (outdoor >= 22 && aircond_intake >= 23) {
    //COOL with AUTO FAN, +25
    operatingMode = MODE_COOL;
    temperature = 25;
    fanSpeed = FAN_AUTO;

    if (outdoor >= 23 && aircond_intake >= 23) {
      //COOL with AUTO FAN, +24
      temperature = 24;
    }
  // Fireplace is hot, use the FAN mode
  } else if (fireplace >= 25) {
    // Default to MODE_FAN with FAN 1
    operatingMode = MODE_FAN;
    temperature = 22;
    fanSpeed = FAN_1;

    if (fireplace >= 26 && fireplace < 28) {
      fanSpeed = FAN_2;
    } else if (fireplace >= 28 && fireplace < 30) {
      fanSpeed = FAN_3;
    } else if (fireplace >= 30 && fireplace < 33) {
      fanSpeed = FAN_4;
    } else if ( fireplace >= 33) {
      fanSpeed = FAN_5;
    }
  // Utility room is hot, as the laundry drier has been running
  } else if ( utility >= 23) {
    // Default to MODE_FAN with FAN 1
    operatingMode = MODE_FAN;
    temperature = 22;
    fanSpeed = FAN_1;

    if (utility >= 24 && utility < 25) {
      fanSpeed = FAN_2;
    } else if ( utility >= 25 && utility < 26) {
      fanSpeed = FAN_3;
    } else if ( utility >= 26 && utility < 27) {
      fanSpeed = FAN_4;
    } else if ( utility >= 26 && utility < 27 && outdoor < 20) {
      fanSpeed = FAN_5;

    } else if (utility >= 27 && outdoor >= 20) {
      // COOL with AUTO FAN, +24
      operatingMode = MODE_COOL;
      temperature = 24;
      fanSpeed = FAN_AUTO;
    }

  // Kitchen is hot, as the oven has been running
  } else if (kitchen >= 23) {
    // Default to MODE_FAN with FAN 1
    operatingMode = MODE_FAN;
    temperature = 22;
    fanSpeed = FAN_1;

    if (kitchen >= 24 && kitchen < 25) {
      fanSpeed = FAN_2;
    } else if ( kitchen >= 25 && kitchen < 26) {
      fanSpeed = FAN_3;
    } else if ( kitchen >= 26 && kitchen < 27) {
      fanSpeed = FAN_4;
    } else if ( kitchen >= 27 && kitchen < 28 && outdoor < 20) {
      fanSpeed = FAN_5;

    } else if (kitchen >= 27 && outdoor >= 20) {
      // COOL with AUTO FAN, +24
      operatingMode = MODE_COOL;
      temperature = 24;
      fanSpeed = FAN_AUTO;
    }

  // Fireplace or utility or kitchen room is not hot
  } else if (outdoor >= 21 && outdoor < 22) {
    // MODE_FAN with FAN_AUTO disable COOL and HEAT replacement all the time
    operatingMode = MODE_FAN;
    temperature = 22;
    fanSpeed = FAN_AUTO;

  // Set the mode based on the outdoor temperature (heating)
  } else {
    // FAN with FAN 1 temp+22
    operatingMode = MODE_HEAT;
    temperature = 22;
    fanSpeed = FAN_AUTO;

    if (outdoor >= 15 && outdoor < 20) {
      // HEAT with AUTO FAN, +23
      temperature = 23;
    } else if (outdoor >= 5 && outdoor < 15) {
      // HEAT with AUTO FAN, +23
      temperature = 23;
    } else if (outdoor >= 0 && outdoor < 5) {
      temperature = 23;
    } else if (outdoor >= -4 && outdoor < 0) {
      temperature = 24;
    } else if (outdoor >= -9 && outdoor < -4) {
      temperature = 25;
    } else if (outdoor >= -14 && outdoor < -9) {
      temperature = 26;
    } else if (outdoor >= -19 && outdoor < -14) {
      temperature = 27;
    } else if (outdoor >= -24 && outdoor < -19) {
      temperature = 28;
    } else if (outdoor >= -40 && outdoor < -24) {
      temperature = 30;
    }
  }


  // Did any of the values change? If so, send an IR command
  if (operatingMode != carrierHeatpump.operatingMode ||
      fanSpeed != carrierHeatpump.fanSpeed ||
      temperature != carrierHeatpump.temperature)
  {
    Serial.println("Sending Carrier command");
    Serial.print("MODE: ");
    Serial.println(operatingMode);
    Serial.print("FAN: ");
    Serial.println(fanSpeed);
    Serial.print("TEMP: ");
    Serial.println(temperature);

    // Save the state
    carrierHeatpump.operatingMode = operatingMode;
    carrierHeatpump.fanSpeed = fanSpeed;
    carrierHeatpump.temperature = temperature;

    // Send the IR command
    heatpumpIR->send(irSender, POWER_ON, carrierHeatpump.operatingMode, carrierHeatpump.fanSpeed, carrierHeatpump.temperature, VDIR_MANUAL, HDIR_MANUAL);
  }
}

//
// Report measurement data to emoncms.org
//
void updateEmoncms() {

  EthernetClient client;
  boolean notFirst = false;
  int emonWaterPulses = 0;

  // Interrupts need to be disabled while the pulse counter is read or modified...
  noInterrupts();
  emonWaterPulses = waterPulses;
  waterPulses = 0;
  interrupts();

  // Update the water use history
  for (byte i=((sizeof(waterPulsesHistory) / sizeof(int)) - 1); i > 0; i--) {
    waterPulsesHistory[i] = waterPulsesHistory[i-1];
  }
  waterPulsesHistory[0] = emonWaterPulses;

  // Report the data into emoncms.org
  Serial.println("Connecting to emoncms.org...");

  if (client.connect("www.emoncms.org", 80)) {
    // send the HTTP GET request:
    client.print("GET http://www.emoncms.org/api/post?apikey=");
    client.print(EMONCMS_APIKEY);
    client.print("&json={");

    // Log sensor temperatures
    for (int i=0; i < sizeof(owbuses) / sizeof(struct owbus); i++) {
      if (owbuses[i].temperature != DEVICE_DISCONNECTED) {
        if (notFirst) {
          client.print(",");
        }
        notFirst = true;

        client.print(owbuses[i].emon_name);
        client.print(":");
        client.print(owbuses[i].temperature);
      }
    }

    // Log heatpump state
    client.print(",heatpump_temp:");
    client.print(carrierHeatpump.temperature);
    client.print(",heatpump_mode:");
    client.print(carrierHeatpump.operatingMode);
    client.print(",heatpump_fanspeed:");
    client.print(carrierHeatpump.fanSpeed);
    client.print(",fireplace_fan:");
    if (carrierHeatpump.fireplaceFan == true) {
      client.print("0");
    } else {
      client.print("1");
    }
    // Log the water meter pulses
    client.print(",water_pulses:");
    client.print(emonWaterPulses);
    // Log the DHT11 readings
    client.print(",dht11_humidity:");
    client.print(DHT11Humidity);
    client.print(",dht11_temperature:");
    client.print(DHT11Temperature);
    // Log the MQ7 readings
    client.print(",mq7_colevel:");
    client.print(MQ7COLevel);
    // Log the MG811 readings
    client.print(",mg811_co2level:");
    client.print(MG811CO2Level);
    // Log the MG811 readings
    client.print(",mg811_Voltage:");
    client.print(MG811Voltage);

    // Log the waterPulsesHistory
    for (byte i=((sizeof(waterPulsesHistory) / sizeof(int)) - 1); i > 0; i--) {
      client.print(",waterPulsesHistory[");
      client.print(i);
      client.print("]:");
      client.print(waterPulsesHistory[i]);
    }

    // Log the alarm state
    client.print(",alarm_state:");
    if ( alarmState == LOW ) {
      client.print("0");
    } else {
      client.print("1");
    }
     // Log the water state
    client.print(",water_state:");
    if ( waterState == LOW ) {
      client.print("0");
    } else {
      client.print("1");
    }
    // Log the leak state
    client.print(",waterLeak_state:");
    if ( waterLeakState == true ) {
      client.print("0");
    } else {
      client.print("1");
    }
    // Log the leak state
    client.print(",showerWaterUse_state:");
    if ( ShowerWaterUse == true ) {
      client.print("0");
    } else {
      client.print("1");
    }
    client.println("} HTTP/1.1");
    client.println("Host: 192.168.0.15");
    client.println("User-Agent: Arduino-ethernet");
    client.println("Connection: close");
    client.println();

    Serial.println(F("\nemoncms.org response:\n---"));
    while (client.connected()) {
      while (client.available()) {
        char c = client.read();
        Serial.print(c);
      }
    }

    Serial.println();
    client.stop();
  }
}

void readSensors() {
  readDHT11();
  readMQ7();
  readMG811();
}


//
// Read the DHT11 humidity & temperature sensor
//
void readDHT11() {
  DHT11Humidity = dht.readHumidity();
  DHT11Temperature = dht.readTemperature();

  // check if returns are valid, if they are NaN (not a number) then something went wrong!
  if (isnan(DHT11Humidity) || isnan(DHT11Temperature)) {
    DHT11Humidity = 0.0;
    DHT11Temperature = 0.0;
  }
}

//
// Read the MQ-7 CO sensor, see http://www.dfrobot.com/wiki/index.php?title=Carbon_Monoxide_Sensor(MQ7)_(SKU:SEN0132)
//
void readMQ7() {
  MQ7COLevel = analogRead(MQ7_PIN); //FREE AIR 51 18.05.2014
}

//
// Read the MG811 CO2 sensor, see http://www.veetech.org.uk/Prototype_CO2_Monitor.htm
//
void readMG811() {
  // Sensor Calibration Constants
  const float v400ppm = 2.21;   //MUST BE SET ACCORDING TO CALIBRATION -> FREE AIR 2.84  USE 2.21 24.06.2014
  const float v40000ppm = 0.80; //MUST BE SET ACCORDING TO CALIBRATION -> FREE AIR 1.87  USE 0.80 24.06.2014
  const float deltavs = v400ppm - v40000ppm;
  const float A = deltavs/(log10(400) - log10(40000));
  const float B = log10(400);

  // Read co2 data from sensor
  int data = analogRead(MG811_PIN); //digitise output from c02 sensor
  MG811Voltage = data/204.6;        //convert output to voltage

  // Calculate co2 from log10 formula (see sensor datasheet)
  float power = ((MG811Voltage - v400ppm)/A) + B;
  MG811CO2Level = pow(10,power);
}


//
// Water use checks
//
void checkForWaterShutoff() {
  checkForWaterUse();
  checkForShowerWaterUse();
  checkForwaterLeak();
}

//
// Check for excessive water use
//
void checkForWaterUse() {

  for (byte i=0; i < (sizeof(waterPulsesHistory) / sizeof(int))-7; i++) {
    // If all samples are 1, shut off water
    if ( waterPulsesHistory[i] == 1 ) { // 1
      continue;
    }
    // If recent samples are 13-14 or over, shut off water
    else if (waterPulsesHistory[0] >= 13 && // 13
             waterPulsesHistory[1] >= 14 && // 14
             waterPulsesHistory[2] >= 13) { // 13
      continue;
    } else {
      return;
    }
  }
 // Water leak - shut off water
  digitalWrite(WATER_STOP_VALVE_PIN, LOW);
  waterState = LOW;
  waterLeakState = true;
}

//
// Check for excessive shower water use
//
void checkForShowerWaterUse() {
}

//
// Check if the water valve needs to be shut due to a leak - same amount of water use for 12 minutes
//
void checkForwaterLeak() {

  int firstWaterPulse = waterPulsesHistory[0];

  for (byte i=0; i < (sizeof(waterPulsesHistory) / sizeof(int)); i++) {
    // It's not a leak if water isn't flowing
    if ( waterPulsesHistory[i] == 0 ) {
      return;
    }
    // If all samples are within +-3 of the first sample, shut off water
    else if ( waterPulsesHistory[i] >= firstWaterPulse-3 && waterPulsesHistory[i] <= firstWaterPulse+3 ) {
      continue;
    } else {
      return;
    }
  }
  // Water leak - shut off water
  digitalWrite(WATER_STOP_VALVE_PIN, LOW);
  waterState = LOW;
  ShowerWaterUse = true;
}

//
// Check if the water valve needs to be open or shut, based on the alarm state
// When the alarm turns off, water will always turn on, and all leak history info
// is cleared
//
void alarmWaterShutoff() {

  if ( alarmState != alarmStateHistory ) {
    // Alarm goes to a different state - clear all leak information so that alarm OFF will
    // always turn water on
    digitalWrite(WATER_STOP_VALVE_PIN, alarmState);

    waterState = alarmState;
    waterLeakState = false;
    ShowerWaterUse = false;

    for (byte i=0; i < (sizeof(waterPulsesHistory) / sizeof(int)); i++) {
      waterPulsesHistory[i] = 0;
    }
  }

  alarmStateHistory = alarmState;
}

//
// Increment the number of water meter pulses
//
void incrementWaterPulses()
{
  waterPulses++;
}


//
// The most important thing of all, feed the watchdog
//
void feedWatchdog()
{
  wdt_reset();
}
