/*
  Arduino sunrise alarm

  @author Stefano Ottolenghi
  @website www.thecrowned.org
*/

#include <TimeLib.h>
#include <TimeAlarms.h>
#include <SPI.h>
#include <Ethernet.h>

#define LED 9
#define BUTTON 2
#define BUZZER 3
#define NIGHT 5 //sensor for night light 

#define DEBUG 1

//For LED circuitry help, see https://www.makeuseof.com/tag/connect-led-light-strips-arduino/

//For F() and PROGMEM, see http://playground.arduino.cc/Learning/Memory
//PROGMEM cannot properly store arrays (or you'd need be cautious when accessing them, see
//https://arduino.stackexchange.com/questions/36120/problem-when-using-progmem-on-array-holding-notes-for-speaker-on-arduino

// Enter a MAC address for your controller below.
const byte mac[] = { 0x00, 0xAA, 0xBB, 0xCC, 0xDE, 0x02 };
//const IPAddress ip(10, 42, 0, 174);

EthernetServer server(23);
EthernetClient client;

const char timeServer[] = "time.nist.gov";
const int NTP_PACKET_SIZE = 48;
byte NTPPacketBuffer[NTP_PACKET_SIZE];
EthernetUDP Udp;
const int UDPLocalPort = 8888;
bool ethStatus;

bool alreadyConnected = false; // whether or not the client was connected previously
bool password = false; //whether user has entered secret keyword
String commandBuffer = "";

const float sunriseDuration = 0.5; //minutes
const float increment = (float) 255 / (sunriseDuration * 60); //each second increase the LED value by
const float buzzerDelay = 1;
float sunriseVal = 0;
bool alarmStopFlag = false; //if true, alarm will stop singing
bool lightsOnManual = false;

/*
   Logs messages to Serial output if output enabled.
   Some manual calls to Serial still present in the code due to non-string messages.
*/
void debug(const __FlashStringHelper* msg) {
  if (DEBUG)
    Serial.println(msg);
}

/*
   Standard Arduino setup function, run once when program starts and never again.
*/
void setup() {
  // Open serial communications and wait for port to open:
  if (DEBUG) {
    Serial.begin(9600);
    while (!Serial) {
      ; // wait for serial port to connect. Needed for native USB port only
    }
  }

  pinMode(LED, OUTPUT);
  pinMode(BUTTON, INPUT);
  pinMode(BUZZER, OUTPUT);
  pinMode(NIGHT, INPUT);
  pinMode(13, OUTPUT);

  //Start the Ethernet connection
  debug(F("Init Ethernet (DHCP):"));
  if (! (ethStatus = Ethernet.begin(mac))) {
    debug(F("Failed to configure Ethernet"));
    if (Ethernet.hardwareStatus() == EthernetNoHardware) {
      debug(F("Ethernet shield not found"));
    } else if (Ethernet.linkStatus() == LinkOFF) {
      debug(F("Ethernet not connected"));
    }

    //If no network, rerun setup. We can do without it after setup, but not before
    if (! ethStatus) {
      delay(5000);
      setup();
    }
  }

  Udp.begin(UDPLocalPort);

  // start listening for clients
  server.begin();

  if (DEBUG) {
    Serial.print(F("Server is at: "));
    Serial.println(Ethernet.localIP());
  }

  setupTime();
}

/*
   Standard Arduino loop function. Executed constantly by microprocessor.
*/
void loop() {
  // wait for a new client:
  client = server.available();

  // when the client sends the first byte, greet them:
  if (client) {
    if (!alreadyConnected) {

      if (DEBUG) {
        Serial.print(F("New client connected, IP: "));
        Serial.println(client.remoteIP());
      }

      client.println(F("Hello client!"));
      client.flush(); // clear out the input buffer
      alreadyConnected = true;
    }

    if (client.available() > 0) {
      char thisChar = client.read();

      if ( thisChar != '\n' ) {
        commandBuffer += String(thisChar);
      } else {
        executeCommand(commandBuffer);
        commandBuffer = String("");
      }
    }
  }

  //renew DHCP lease
  Ethernet.maintain();

  Alarm.delay(50); // wait one second between clock display: low good for testing

  maybeNightLight();
}

/*
   Enabled night light if sensor is triggered.
*/
void maybeNightLight() {
  if (digitalRead(NIGHT) == LOW and lightsOnManual == false) {
    debug(F("~~ Activating night light ~~"));
    lightsOnManual = true;
    analogWrite(LED, 20);
  } else if (digitalRead(NIGHT) == HIGH and lightsOnManual == true) {
    debug(F("~~ De-activating night light ~~"));
    analogWrite(LED, 0);
    lightsOnManual = false;
  }
}

/*
   Prints current time to telnet client.
*/
void printTime(time_t timestamp) {
  client.print(year(timestamp));
  client.print(F("-"));
  client.print(month(timestamp));
  client.print(F("-"));
  client.print(day(timestamp));
  client.print(F(" "));
  client.print(hour(timestamp));
  client.print(F(":"));
  client.print(minute(timestamp));
  client.print(F(":"));
  client.println(second(timestamp));
}

/*
   Executes a command arrived through telnet.
*/
void executeCommand(String command) {
  command = command.substring(0, ( command.length() - 1 ) ); //there's a trailing BR (hex D)

  if (command == String("feynman")) {
    password = true;
    client.println(F("Access granted"));
    debug(F("Access granted"));
    return;
  }

  if (password == false) return;

  if (DEBUG) {
    Serial.println(F("\n> EXECUTING COMMAND: "));
    Serial.print(command);
  }

  if ( command == String("help") ) {
    debug(F(" (Help)"));
    client.println(F("print time"));
    client.println(F("show alarms"));
    client.println(F("set alarm"));
    client.println(F("clear alarms"));
    client.println(F("lights on [n]"));
    client.println(F("lights off"));
    client.println(F("lights rainbow"));
    client.println(F("sing"));
    client.println(F("update time"));
    client.println(F("exit"));
    
  } else if ( command == String("exit") ) {
    debug(F(" (Termino sessione)"));

    alreadyConnected = false;
    password = false;

    client.stop();
    
  } else if ( command.indexOf( "set alarm" ) != -1 ) {
    debug(F(" (Set alarm time)"));

    time_t sunriseAlarmTime = AlarmHMS(command.substring(10, 12).toInt(), //10 to skip trailing space as well
                                command.substring(13, 15).toInt(),
                                0);
    time_t buzzerAlarmTime = sunriseAlarmTime + (sunriseDuration + buzzerDelay)*60;
    time_t stopAlarmTime = sunriseAlarmTime + (sunriseDuration + buzzerDelay + 2)*60;

    Alarm.alarmRepeat(hour(sunriseAlarmTime), minute(sunriseAlarmTime), second(sunriseAlarmTime), sunriseKickstart);
    Alarm.alarmRepeat(hour(buzzerAlarmTime), minute(buzzerAlarmTime), second(buzzerAlarmTime), sing);
    Alarm.alarmRepeat(hour(stopAlarmTime), minute(stopAlarmTime), second(stopAlarmTime), alarmStop);
    
  } else if ( command.indexOf( "print time" ) != -1 ) {
    debug(F(" (Print current time)"));
    printTime(now());
    
  } else if ( command.indexOf( "clear alarms" ) != -1 ) {
    debug(F(" (Clear all alarms)"));

    for (uint8_t id = 0; id < dtNBR_ALARMS; id++) {
      if (Alarm.isAllocated(id)) {
        Alarm.free(id);   // ensure all Alarms are cleared and available for allocation
        Serial.println(id);
      }
    }
    
  } else if ( command.indexOf( "show alarms" ) != -1 ) {
    debug(F(" (Show alarms)"));
    for (uint8_t id = 0; id < dtNBR_ALARMS; id++) {
      if (Alarm.isAllocated(id)) {
        printTime(Alarm.getNextTrigger(id));
      }
    }
    
  } else if (command.indexOf("lights on") != -1) {
    debug(F(" (Lights on)"));
    int brightness = 255;
    if (command.length() > 10) {
      brightness = command.substring(10, command.length()).toInt();
    }
    analogWrite(LED, brightness);
    
  } else if (command.indexOf("lights off") != -1) {
    debug(F(" (Lights off)"));
    analogWrite(LED, 0);
    
  } else if (command.indexOf("lights rainbow") != -1) {
    debug(F(" (Lights rainbow)"));
    for (int i = 0; i < 255; i++) {
      analogWrite(LED, random(0, 255));
      delay(200);
    }
    analogWrite(LED, 0);
    
  } else if (command.indexOf("sing") != -1) {
    debug(F(" (Sing tune)"));
    sing();
    
  } else if (command.indexOf("update time") != -1) {
    debug(F(" (Update time)"));
    setupTime();
  }
}

/*
   Starts sunrise when alarm is triggered.
*/
void sunriseKickstart() {
  debug(F("\n~~ Starting sunrise ~~"));

  sunrise();
}

/*
   Stops alarm.
   Needs to be another function triggered by another alarm,
   otherwise Arduino would be blocked in all the time in which the alarm is firing.
*/
void alarmStop() {
  debug(F("~~ Stopping alarm ~~"));
  analogWrite(LED, 0);
  alarmStopFlag = true;
  sunriseVal = 0;
}

/*
   Makes the sunrise happen.
   Re-schedules itself one second later so that Arduino is not blocked while sunrise is happening.
*/
void sunrise() {
  if (sunriseVal < 255) {
    sunriseVal += increment;
    
    if (DEBUG) {
      Serial.print(F("Current value for sunrise: "));
      Serial.println(sunriseVal);
    }

    analogWrite(LED, sunriseVal);
    Alarm.alarmOnce(hour(now()+1), minute(now()+1), second(now()+1), sunrise);
    
    //delay(1000);
    //sunrise(sunriseVal);
  } else {
    debug(F("~~ Keep lights on for a while... ~~"));
  }
}

/*
   Arduino does not have any internal clock, so we must care for the time.
   We retrieve it through a NTP server and then keep track of it.

   https://www.arduino.cc/en/Tutorial/UdpNtpClient
*/
void setupTime() {
  sendNTPpacket(timeServer); // send an NTP packet to a time server

  // wait to see if a reply is available
  delay(2000);

  if (Udp.parsePacket()) {
    // We've received a packet, read the data from it
    debug(F("NTP packet received..."));
    Udp.read(NTPPacketBuffer, NTP_PACKET_SIZE); // read the packet into the buffer

    // the timestamp starts at byte 40 of the received packet and is four bytes,
    // or two words, long. First, extract the two words:
    unsigned long highWord = word(NTPPacketBuffer[40], NTPPacketBuffer[41]);
    unsigned long lowWord = word(NTPPacketBuffer[42], NTPPacketBuffer[43]);
    // combine the four bytes (two words) into a long integer
    // this is NTP time (seconds since Jan 1 1900):
    unsigned long secsSince1900 = highWord << 16 | lowWord;

    // now convert NTP time into everyday time (UNIX timestamp):
    // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
    const unsigned long seventyYears = 2208988800UL;
    // subtract seventy years:
    time_t epoch = secsSince1900 - seventyYears;

    //Set current time
    setTime(epoch);

    //Set local time
    adjustTime(3600);

    if (DEBUG) {
      Serial.print(F("Local time is "));
      Serial.println(now());
    }
  } else {
    setupTime();
  }
}

/*
   Send an NTP request to the time server at the given address.
*/
void sendNTPpacket(const char * address) {
  debug(F("Sending NTP packet..."));

  // set all bytes in the buffer to 0
  memset(NTPPacketBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  NTPPacketBuffer[0] = 0b11100011;   // LI, Version, Mode
  NTPPacketBuffer[1] = 0;     // Stratum, or type of clock
  NTPPacketBuffer[2] = 6;     // Polling Interval
  NTPPacketBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  NTPPacketBuffer[12]  = 49;
  NTPPacketBuffer[13]  = 0x4E;
  NTPPacketBuffer[14]  = 49;
  NTPPacketBuffer[15]  = 52;

  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  Udp.beginPacket(address, 123); // NTP requests are to port 123
  Udp.write(NTPPacketBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}

/*
   https://www.princetronics.com/supermariothemesong/
*/
#define NOTE_B0  31
#define NOTE_C1  33
#define NOTE_CS1 35
#define NOTE_D1  37
#define NOTE_DS1 39
#define NOTE_E1  41
#define NOTE_F1  44
#define NOTE_FS1 46
#define NOTE_G1  49
#define NOTE_GS1 52
#define NOTE_A1  55
#define NOTE_AS1 58
#define NOTE_B1  62
#define NOTE_C2  65
#define NOTE_CS2 69
#define NOTE_D2  73
#define NOTE_DS2 78
#define NOTE_E2  82
#define NOTE_F2  87
#define NOTE_FS2 93
#define NOTE_G2  98
#define NOTE_GS2 104
#define NOTE_A2  110
#define NOTE_AS2 117
#define NOTE_B2  123
#define NOTE_C3  131
#define NOTE_CS3 139
#define NOTE_D3  147
#define NOTE_DS3 156
#define NOTE_E3  165
#define NOTE_F3  175
#define NOTE_FS3 185
#define NOTE_G3  196
#define NOTE_GS3 208
#define NOTE_A3  220
#define NOTE_AS3 233
#define NOTE_B3  247
#define NOTE_C4  262
#define NOTE_CS4 277
#define NOTE_D4  294
#define NOTE_DS4 311
#define NOTE_E4  330
#define NOTE_F4  349
#define NOTE_FS4 370
#define NOTE_G4  392
#define NOTE_GS4 415
#define NOTE_A4  440
#define NOTE_AS4 466
#define NOTE_B4  494
#define NOTE_C5  523
#define NOTE_CS5 554
#define NOTE_D5  587
#define NOTE_DS5 622
#define NOTE_E5  659
#define NOTE_F5  698
#define NOTE_FS5 740
#define NOTE_G5  784
#define NOTE_GS5 831
#define NOTE_A5  880
#define NOTE_AS5 932
#define NOTE_B5  988
#define NOTE_C6  1047
#define NOTE_CS6 1109
#define NOTE_D6  1175
#define NOTE_DS6 1245
#define NOTE_E6  1319
#define NOTE_F6  1397
#define NOTE_FS6 1480
#define NOTE_G6  1568
#define NOTE_GS6 1661
#define NOTE_A6  1760
#define NOTE_AS6 1865
#define NOTE_B6  1976
#define NOTE_C7  2093
#define NOTE_CS7 2217
#define NOTE_D7  2349
#define NOTE_DS7 2489
#define NOTE_E7  2637
#define NOTE_F7  2794
#define NOTE_FS7 2960
#define NOTE_G7  3136
#define NOTE_GS7 3322
#define NOTE_A7  3520
#define NOTE_AS7 3729
#define NOTE_B7  3951
#define NOTE_C8  4186
#define NOTE_CS8 4435
#define NOTE_D8  4699
#define NOTE_DS8 4978

//Underworld melody
const int underworld_melody[] = {
  NOTE_C4, NOTE_C5, NOTE_A3, NOTE_A4,
  NOTE_AS3, NOTE_AS4, 0,
  0,
  NOTE_C4, NOTE_C5, NOTE_A3, NOTE_A4,
  NOTE_AS3, NOTE_AS4, 0,
  0,
  NOTE_F3, NOTE_F4, NOTE_D3, NOTE_D4,
  NOTE_DS3, NOTE_DS4, 0,
  0,
  NOTE_F3, NOTE_F4, NOTE_D3, NOTE_D4,
  NOTE_DS3, NOTE_DS4, 0,
  0, NOTE_DS4, NOTE_CS4, NOTE_D4,
  NOTE_CS4, NOTE_DS4,
  NOTE_DS4, NOTE_GS3,
  NOTE_G3, NOTE_CS4,
  NOTE_C4, NOTE_FS4, NOTE_F4, NOTE_E3, NOTE_AS4, NOTE_A4,
  NOTE_GS4, NOTE_DS4, NOTE_B3,
  NOTE_AS3, NOTE_A3, NOTE_GS3,
  0, 0, 0
};

//Underworld tempo
const int underworld_tempo[] = {
  12, 12, 12, 12,
  12, 12, 6,
  3,
  12, 12, 12, 12,
  12, 12, 6,
  3,
  12, 12, 12, 12,
  12, 12, 6,
  3,
  12, 12, 12, 12,
  12, 12, 6,
  6, 18, 18, 18,
  6, 6,
  6, 6,
  6, 6,
  18, 18, 18, 18, 18, 18,
  10, 10, 10,
  10, 10, 10,
  3, 3, 3
};

/*
   Sings the super mario tune until the stop button is pushed.
*/
void sing() {
  debug(F("Singing a tune..."));
  delay(2000); //pause between one alarm and the other

  // iterate over the notes of the melody:
  int size = sizeof(underworld_melody) / sizeof(int);
  for (int thisNote = 0; thisNote < size; thisNote++) {

    //Look for stop trigger
    if (digitalRead(BUTTON) == HIGH) {
      debug(F("Stop singing!"));
      alarmStop();
      return;
    }

    // to calculate the note duration, take one second divided by the note type.
    int noteDuration = 1000 / underworld_tempo[thisNote];

    buzz(underworld_melody[thisNote], noteDuration);

    // to distinguish the notes, set a minimum time between them. The note's duration + 30% seems to work well:
    delay(noteDuration * 1.30);

    // stop the tone playing:
    buzz(0, noteDuration);
  }

  sing(); //keep singing until turned off with button
}

/*
   Plays a single note through the buzzer.
*/
void buzz(long frequency, long length) {
  digitalWrite(13, HIGH);
  long delayValue = 1000000 / frequency / 2; // calculate the delay value between transitions
  // 1 second's worth of microseconds, divided by the frequency, then split in half since
  // there are two phases to each cycle
  long numCycles = frequency * length / 1000; // calculate the number of cycles for proper timing
  // multiply frequency, which is really cycles per second, by the number of seconds to
  // get the total number of cycles to produce
  for (long i = 0; i < numCycles; i++) { // for the calculated length of time...
    digitalWrite(BUZZER, HIGH); // write the buzzer pin high to push out the diaphram
    delayMicroseconds(delayValue); // wait for the calculated delay value
    digitalWrite(BUZZER, LOW); // write the buzzer pin low to pull back the diaphram
    delayMicroseconds(delayValue); // wait again or the calculated delay value
  }
  digitalWrite(13, LOW);
}
