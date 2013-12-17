#include <LiquidCrystal.h>
#include <LCDKeypad.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <DS18B20.h>
#include <SPI.h>
#include <Ethernet.h>
#include <Timer.h>

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
    operatingMode = 5;
    temperature = 22;
    fanSpeed = 3;

    if (fireplace >= 40 && fireplace < 45) {
      fanSpeed = 4;
    } else if ( fireplace >= 45) {
      fanSpeed = 5;
    }

  // Utility room is hot, as the laundry drier has been running
  } else if (utility > 24 ) {
    // FAN with FAN 3
    operatingMode = 5;
    temperature = 22;
    fanSpeed = 3;

    if (utility >= 26 && utility < 28) {
      fanSpeed = 4;
    } else if ( utility >= 28 && utility < 29) {
      fanSpeed = 5;
    } else if (utility >= 29) {
      // COOL with AUTO FAN, +24
      operatingMode = 1;
      temperature = 24;
      fanSpeed = 0;
    }

  // Fireplace or utility room is not hot,
  // set the mode based on the outdoor temperature
  } else {
    if (outdoor > 28) {
      // COOL with AUTO FAN, +24
      operatingMode = 1;
      temperature = 24;
      fanSpeed = 0;
    } else if (outdoor >= 5 && outdoor < 15) {
      // HEAT with AUTO FAN, +23
      operatingMode = 2;
      temperature = 23;
      fanSpeed = 0;
    } else if (outdoor >= 0 && outdoor < 5) {
      operatingMode = 2;
      temperature = 23;
      fanSpeed = 0;
    } else if (outdoor >= -4 && outdoor < 0) {
      operatingMode = 2;
      temperature = 24;
      fanSpeed = 0;
    } else if (outdoor >= -9 && outdoor < -4) {
      operatingMode = 2;
      temperature = 25;
      fanSpeed = 0;
    } else if (outdoor >= -14 && outdoor < -9) {
      operatingMode = 2;
      temperature = 26;
      fanSpeed = 0;
    } else if (outdoor >= -19 && outdoor < -14) {
      operatingMode = 2;
      temperature = 27;
      fanSpeed = 0;
    } else if (outdoor >= -24 && outdoor < -19) {
      operatingMode = 2;
      temperature = 28;
      fanSpeed = 0;
    } else if (outdoor >= -40 && outdoor < -24) {
      operatingMode = 2;
      temperature = 30;
      fanSpeed = 0;
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
    sendCarrierCmd(1, carrierHeatpump.operatingMode, carrierHeatpump.fanSpeed, carrierHeatpump.temperature, 1, 1);
  }
}



//
//
// IR signal transmit -specific things
//
//


#define IR_USE_TIMER5   // tx = pin 46

#ifdef F_CPU
#define SYSCLOCK F_CPU // main Arduino clock
#else
#define SYSCLOCK 16000000 // main Arduino clock
#endif

// defines for timer5 (16 bits)
#if defined(IR_USE_TIMER5)
#define TIMER_RESET
#define TIMER_ENABLE_PWM (TCCR5A |= _BV(COM5A1))
#define TIMER_DISABLE_PWM (TCCR5A &= ~(_BV(COM5A1)))
#define TIMER_ENABLE_INTR (TIMSK5 = _BV(OCIE5A))
#define TIMER_DISABLE_INTR (TIMSK5 = 0)
#define TIMER_INTR_NAME TIMER5_COMPA_vect
#define TIMER_CONFIG_KHZ(val) ({ \
const uint16_t pwmval = SYSCLOCK / 2000 / (val); \
TCCR5A = _BV(WGM51); \
TCCR5B = _BV(WGM53) | _BV(CS50); \
ICR5 = pwmval; \
OCR5A = pwmval / 3; \
})
#define TIMER_CONFIG_NORMAL() ({ \
TCCR5A = 0; \
TCCR5B = _BV(WGM52) | _BV(CS50); \
OCR5A = SYSCLOCK * USECPERTICK / 1000000; \
TCNT5 = 0; \
})
#if defined(CORE_OC5A_PIN)
#define TIMER_PWM_PIN CORE_OC5A_PIN
#elif defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
#define TIMER_PWM_PIN 46 /* Arduino Mega */
#else
#error "Please add OC5A pin number here\n"
#endif
#else // unknown timer
#error "Internal code configuration error, no known IR_USE_TIMER# defined\n"
#endif


void mark(int time) {
  // Sends an IR mark for the specified number of microseconds.
  // The mark output is modulated at the PWM frequency.
  TIMER_ENABLE_PWM; // Enable pin 3 PWM output
  delayMicroseconds(time);
}

/* Leave pin off for time (given in microseconds) */
void space(int time) {
  // Sends an IR space for the specified number of microseconds.
  // A space is no output, so the PWM output is disabled.
  TIMER_DISABLE_PWM; // Disable pin 3 PWM output
  delayMicroseconds(time);
}


void enableIROut(int khz) {
  // Enables IR output. The khz value controls the modulation frequency in kilohertz.
  // The IR output will be on pin 3 (OC2B).
  // This routine is designed for 36-40KHz; if you use it for other values, it's up to you
  // to make sure it gives reasonable results. (Watch out for overflow / underflow / rounding.)
  // TIMER2 is used in phase-correct PWM mode, with OCR2A controlling the frequency and OCR2B
  // controlling the duty cycle.
  // There is no prescaling, so the output frequency is 16MHz / (2 * OCR2A)
  // To turn the output on and off, we leave the PWM running, but connect and disconnect the output pin.
  // A few hours staring at the ATmega documentation and this will all make sense.
  // See my Secrets of Arduino PWM at http://arcfn.com/2009/07/secrets-of-arduino-pwm.html for details.


  // Disable the Timer2 Interrupt (which is used for receiving IR)
  TIMER_DISABLE_INTR; //Timer2 Overflow Interrupt

  pinMode(TIMER_PWM_PIN, OUTPUT);
  digitalWrite(TIMER_PWM_PIN, LOW); // When not sending PWM, we want it low

  // COM2A = 00: disconnect OC2A
  // COM2B = 00: disconnect OC2B; to send signal set to 10: OC2B non-inverted
  // WGM2 = 101: phase-correct PWM with OCRA as top
  // CS2 = 000: no prescaling
  // The top value for the timer. The modulation frequency will be SYSCLOCK / 2 / OCR2A.
  TIMER_CONFIG_KHZ(khz);
}


// Send a byte over IR

void sendIRByte(byte sendByte, int bitMarkLength, int zeroSpaceLength, int oneSpaceLength)
{
  for (int i=0; i<8 ; i++)
  {
    if (sendByte & 0x01)
    {
      mark(bitMarkLength);
      space(oneSpaceLength);
    }
    else
    {
      mark(bitMarkLength);
      space(zeroSpaceLength);
    }

    sendByte >>= 1;
  }
}


//
//
// Carrier heatpump -specific things
//
//


// Carrier 42NQV035G / 38NYV035H2 (remote control WH-L05SE) timing constants
#define CARRIER_AIRCON1_HDR_MARK   4320
#define CARRIER_AIRCON1_HDR_SPACE  4350
#define CARRIER_AIRCON1_BIT_MARK   500
#define CARRIER_AIRCON1_ONE_SPACE  1650
#define CARRIER_AIRCON1_ZERO_SPACE 550
#define CARRIER_AIRCON1_MSG_SPACE  7400

// Carrier 42NQV035G / 38NYV035H2 (remote control WH-L05SE) codes
#define CARRIER_AIRCON1_MODE_AUTO  0x00 // Operating mode
#define CARRIER_AIRCON1_MODE_HEAT  0xC0
#define CARRIER_AIRCON1_MODE_COOL  0x80
#define CARRIER_AIRCON1_MODE_DRY   0x40
#define CARRIER_AIRCON1_MODE_FAN   0x20
#define CARRIER_AIRCON1_MODE_OFF   0xE0 // Power OFF
#define CARRIER_AIRCON1_FAN_AUTO   0x00 // Fan speed
#define CARRIER_AIRCON1_FAN1       0x02
#define CARRIER_AIRCON1_FAN2       0x06
#define CARRIER_AIRCON1_FAN3       0x01
#define CARRIER_AIRCON1_FAN4       0x05
#define CARRIER_AIRCON1_FAN5       0x03


// Send the Carrier code
// Carrier has the LSB and MSB in different format than Panasonic

void sendCarrier(byte operatingMode, byte fanSpeed, byte temperature)
{
  byte sendBuffer[9] = { 0x4f, 0xb0, 0xc0, 0x3f, 0x80, 0x00, 0x00, 0x00, 0x00 }; // The data is on the last four bytes

  static prog_uint8_t temperatures[] PROGMEM = { 0x00, 0x08, 0x04, 0x0c, 0x02, 0x0a, 0x06, 0x0e, 0x01, 0x09, 0x05, 0x0d, 0x03, 0x0b };
  byte checksum = 0;

  sendBuffer[5] = temperatures[(temperature-17)];
  sendBuffer[6] = operatingMode | fanSpeed;

  // Checksum

  for (int i=0; i<8; i++) {
    checksum += Bit_Reverse(sendBuffer[i]);
  }

  sendBuffer[8] = Bit_Reverse(checksum);

  // 40 kHz PWM frequency
  enableIROut(40);

  // Header
  mark(CARRIER_AIRCON1_HDR_MARK);
  space(CARRIER_AIRCON1_HDR_SPACE);

  // Payload
  for (int i=0; i<sizeof(sendBuffer); i++) {
    sendIRByte(sendBuffer[i], CARRIER_AIRCON1_BIT_MARK, CARRIER_AIRCON1_ZERO_SPACE, CARRIER_AIRCON1_ONE_SPACE);
  }

  // Pause + new header
  mark(CARRIER_AIRCON1_BIT_MARK);
  space(CARRIER_AIRCON1_MSG_SPACE);

  mark(CARRIER_AIRCON1_HDR_MARK);
  space(CARRIER_AIRCON1_HDR_SPACE);

  // Payload again
  for (int i=0; i<sizeof(sendBuffer); i++) {
    sendIRByte(sendBuffer[i], CARRIER_AIRCON1_BIT_MARK, CARRIER_AIRCON1_ZERO_SPACE, CARRIER_AIRCON1_ONE_SPACE);
  }

  // End mark
  mark(CARRIER_AIRCON1_BIT_MARK);
  space(0);
}

// See http://www.nrtm.org/index.php/2013/07/25/reverse-bits-in-a-byte/
byte Bit_Reverse( byte x )
{
  //          01010101  |         10101010
  x = ((x >> 1) & 0x55) | ((x << 1) & 0xaa);
  //          00110011  |         11001100
  x = ((x >> 2) & 0x33) | ((x << 2) & 0xcc);
  //          00001111  |         11110000
  x = ((x >> 4) & 0x0f) | ((x << 4) & 0xf0);
  return x;
}


// Carrier 42NQV035G / 38NYV035H2 (remote control WH-L05SE) numeric values to command bytes

void sendCarrierCmd(byte powerModeCmd, byte operatingModeCmd, byte fanSpeedCmd, byte temperatureCmd, byte swingVCmd, byte swingHCmd)
{
  // Sensible defaults for the heat pump mode

  byte operatingMode = CARRIER_AIRCON1_MODE_HEAT;
  byte fanSpeed = CARRIER_AIRCON1_FAN_AUTO;
  byte temperature = 23;

  if (powerModeCmd == 0)
  {
    operatingMode = CARRIER_AIRCON1_MODE_OFF;
  }
  else
  {
    switch (operatingModeCmd)
    {
      case 1:
        operatingMode = CARRIER_AIRCON1_MODE_AUTO;
        break;
      case 2:
        operatingMode = CARRIER_AIRCON1_MODE_HEAT;
        break;
      case 3:
        operatingMode = CARRIER_AIRCON1_MODE_COOL;
        break;
      case 4:
        operatingMode = CARRIER_AIRCON1_MODE_DRY;
        break;
      case 5:
        operatingMode = CARRIER_AIRCON1_MODE_FAN;
        temperatureCmd = 22; // Temperature is always 22 on FAN mode
        break;
    }
  }

  switch (fanSpeedCmd)
  {
    case 1:
      fanSpeed = CARRIER_AIRCON1_FAN_AUTO;
      break;
    case 2:
      fanSpeed = CARRIER_AIRCON1_FAN1;
      break;
    case 3:
      fanSpeed = CARRIER_AIRCON1_FAN2;
      break;
    case 4:
      fanSpeed = CARRIER_AIRCON1_FAN3;
      break;
    case 5:
      fanSpeed = CARRIER_AIRCON1_FAN4;
      break;
    case 6:
      fanSpeed = CARRIER_AIRCON1_FAN5;
      break;
  }

  if ( temperatureCmd > 16 && temperatureCmd < 31)
  {
    temperature = temperatureCmd;
  }

  sendCarrier(operatingMode, fanSpeed, temperature);
}

