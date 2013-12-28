#include <LiquidCrystal.h>
#include <LCDKeypad.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <DS18B20.h>
#include <SPI.h>
#include <Ethernet.h>
#include <Timer.h>
#include <CarrierHeatpumpIR.h> // From HeatpumpIR library, https://github.com/ToniA/arduino-heatpumpir/archive/master.zip

// Use digital pins 4, 5, 6, 7, 8, 9, 10, and analog pin 0 to interface with the LCD
// Do not use Pin 10 while this shield is connected
LCDKeypad lcd;

// DS18B20 sensor wire colors:
// * Red: Vcc
// * Yellow: Ground
// * Green: Data

// Four 1-wire buses
OneWire ow0(30);
DallasTemperature owsensors0(&ow0);
OneWire ow1(32);
DallasTemperature owsensors1(&ow1);
OneWire ow2(34);
DallasTemperature owsensors2(&ow2);
OneWire ow3(36);
DallasTemperature owsensors3(&ow3);

// Structure to hold them
typedef struct owbus owbus;
struct owbus
{
    DallasTemperature owbus;
    float temperature;
    char* name;
};

// and the array
owbus owbuses[] = {
  {owsensors0, DEVICE_DISCONNECTED, "Takka"},    // Fireplace
  {owsensors1, DEVICE_DISCONNECTED, "ILP imu"},  // Carrier heatpump intake air
  {owsensors2, DEVICE_DISCONNECTED, "KHH"},      // Utility room
  {owsensors3, DEVICE_DISCONNECTED, "Ulkoilma"}  // Outdoor
};


// Structure for the Carrier mode
typedef struct CarrierHeatpump CarrierHeatpump;
struct CarrierHeatpump
{
  int operatingMode;
  int fanSpeed;
  int temperature;
};

// Default mode for the heatpump is HEAT, AUTO FAN and 23 degrees
CarrierHeatpump carrierHeatpump = { 2, 0, 23 };

// The Carrier heatpump instance, and the IRSender instance
HeatpumpIR *heatpumpIR = new CarrierHeatpumpIR();
IRSender irSender(54); // IR led on Mega digital pin 54

// The number of the displayed sensor
int displayedSensor = 0;

// MAC address for the Ethernet shield
byte macAddress[6] = { 0x02, 0x26, 0x89, 0x00, 0x00, 0xFE};

// The timers
Timer timer;


void setup()
{
  // Serial initialization

  Serial.begin(9600);
  delay(500);
  Serial.println(F("Starting..."));

  lcd.begin(16, 2);
  lcd.clear();
  lcd.print("K\xE1ynnistyy..."); // 'Starting...'
  delay(1000);

  // List OneWire devices

  for (int i=0; i < sizeof(owbuses) / sizeof(struct owbus); i++)
  {
    lcd.clear();
    lcd.print(owbuses[i].name);
    lcd.setCursor(0, 1);
    lcd.print("laitteita: "); // 'devices'
    Serial.print(F("Väylässä ")); // 'In bus'
    Serial.print(owbuses[i].name);
    Serial.print(F(" on ")); // 'there is'
    owbuses[i].owbus.begin();
    owbuses[i].owbus.setWaitForConversion(false);
    int deviceCount = owbuses[i].owbus.getDeviceCount();
    Serial.print(deviceCount);
    Serial.println(" laitetta"); // 'devices'
    lcd.print(deviceCount);
    delay(1000);
  }

/*
  Serial.println("Obtaining IP address from DHCP server...");

  // initialize the Ethernet adapter with DHCP
  if(Ethernet.begin(macAddress) == 0) {
    Serial.println("Failed to configure Ethernet using DHCP");
  }

  delay(1000); // give the Ethernet shield a second to initialize

  Serial.print("IP address from DHCP server: ");
  Serial.println(Ethernet.localIP());
*/

  // The timed calls
  timer.every(2000, showTemperatures);  // every 2 seconds
  timer.every(300000L, controlCarrier); // every 5 minutes
}

void loop()
{
  timer.update();
}



void showTemperatures()
{
  // The last display is the 'MODE' display
  if ( displayedSensor < sizeof(owbuses) / sizeof(struct owbus)) {
    // Measure and display the temperature
    owbuses[displayedSensor].owbus.requestTemperatures();
    delay(750);
    owbuses[displayedSensor].temperature = owbuses[displayedSensor].owbus.getTempCByIndex(0);

    // Display the device name and temperature on the LCD
    lcd.clear();
    lcd.print(owbuses[displayedSensor].name);
    lcd.setCursor(0, 1);
    lcd.print(owbuses[displayedSensor].temperature);

    // And the same on debug display
    Serial.print(owbuses[displayedSensor].name);
    Serial.print(F(": "));
    Serial.println(owbuses[displayedSensor].temperature);
    displayedSensor++;
  } else {
    displayedSensor = 0;

    delay(750);
    Serial.print("MODE: ");
    Serial.println(carrierHeatpump.operatingMode);
    Serial.print("FAN: ");
    Serial.println(carrierHeatpump.fanSpeed);
    Serial.print("TEMP: ");
    Serial.println(carrierHeatpump.temperature);

    lcd.clear();
    lcd.print("MODE: ");
    switch (carrierHeatpump.operatingMode) {
      case 1:
        lcd.print("COOL");
        break;
      case 2:
        lcd.print("HEAT");
        break;
      case 5:
        lcd.print("FAN");
        break;
    }

    lcd.setCursor(0, 1);
    lcd.print("TEMP: ");
    lcd.print(carrierHeatpump.temperature);

    lcd.setCursor(10, 1);
    lcd.print("FAN: ");
    lcd.print(carrierHeatpump.fanSpeed);
  }
}

// Decide what to do with the Carrier heatpump

void controlCarrier()
{
  Serial.println("Controlling Carrier");

  int operatingMode = 2;
  int fanSpeed = 0;
  int temperature = 23;

  int outdoor = owbuses[3].temperature;
  int fireplace = owbuses[0].temperature;
  int utility = owbuses[2].temperature;

  // Fireplace is hot, use the FAN mode
  if (fireplace > 35) {
    // FAN with FAN 3
    operatingMode = MODE_FAN;
    temperature = 22;
    fanSpeed = FAN_3;

    if (fireplace >= 40 && fireplace < 45) {
      fanSpeed = FAN_4;
    } else if ( fireplace >= 45) {
      fanSpeed = FAN_5;
    }

  // Utility room is hot, as the laundry drier has been running
  } else if (utility > 24 ) {
    // FAN with FAN 3
    operatingMode = MODE_FAN;
    temperature = 22;
    fanSpeed = FAN_3;

    if (utility >= 26 && utility < 28) {
      fanSpeed = FAN_4;
    } else if ( utility >= 28 && utility < 29) {
      fanSpeed = FAN_5;
    } else if (utility >= 29) {
      // COOL with AUTO FAN, +24
      operatingMode = MODE_COOL;
      temperature = 24;
      fanSpeed = FAN_AUTO;
    }

  // Fireplace or utility room is not hot,
  // set the mode based on the outdoor temperature
  } else {
    if (outdoor > 28) {
      // COOL with AUTO FAN, +24
      operatingMode = MODE_COOL;
      temperature = 24;
      fanSpeed = FAN_AUTO;
    } else if (outdoor >= 5 && outdoor < 15) {
      // HEAT with AUTO FAN, +23
      operatingMode = MODE_HEAT;
      temperature = 23;
      fanSpeed = FAN_AUTO;
    } else if (outdoor >= 0 && outdoor < 5) {
      operatingMode = MODE_HEAT;
      temperature = 23;
      fanSpeed = FAN_AUTO;
    } else if (outdoor >= -4 && outdoor < 0) {
      operatingMode = MODE_HEAT;
      temperature = 24;
      fanSpeed = FAN_AUTO;
    } else if (outdoor >= -9 && outdoor < -4) {
      operatingMode = MODE_HEAT;
      temperature = 25;
      fanSpeed = FAN_AUTO;
    } else if (outdoor >= -14 && outdoor < -9) {
      operatingMode = MODE_HEAT;
      temperature = 26;
      fanSpeed = FAN_AUTO;
    } else if (outdoor >= -19 && outdoor < -14) {
      operatingMode = MODE_HEAT;
      temperature = 27;
      fanSpeed = FAN_AUTO;
    } else if (outdoor >= -24 && outdoor < -19) {
      operatingMode = MODE_HEAT;
      temperature = 28;
      fanSpeed = FAN_AUTO;
    } else if (outdoor >= -40 && outdoor < -24) {
      operatingMode = MODE_HEAT;
      temperature = 30;
      fanSpeed = FAN_AUTO;
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
