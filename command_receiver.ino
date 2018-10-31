/*
  DHCP-based IP printer

  This sketch uses the DHCP extensions to the Ethernet library
  to get an IP address via DHCP and print the address obtained.
  using an Arduino Wiznet Ethernet shield.

  Circuit:
   Ethernet shield attached to pins 10, 11, 12, 13

  created 12 April 2011
  modified 9 Apr 2012
  by Tom Igoe
  modified 02 Sept 2015
  by Arturo Guadalupi

 */

#include <TimeLib.h>
#include <TimeAlarms.h>
#include <SPI.h>
#include <Ethernet.h>

// Enter a MAC address for your controller below.
// Newer Ethernet shields have a MAC address printed on a sticker on the shield
byte mac[] = {
  0x00, 0xAA, 0xBB, 0xCC, 0xDE, 0x02
};

EthernetServer server(23);
EthernetClient client;

const char timeServer[] = "time.nist.gov";
const int NTP_PACKET_SIZE = 48;
byte NTPPacketBuffer[NTP_PACKET_SIZE];
EthernetUDP Udp;
const int UDPLocalPort = 8888;

boolean alreadyConnected = false; // whether or not the client was connected previously
String command_buffer = "";

void setup() {
  // You can use Ethernet.init(pin) to configure the CS pin
  //Ethernet.init(10);  // Most Arduino shields
  //Ethernet.init(5);   // MKR ETH shield
  //Ethernet.init(0);   // Teensy 2.0
  //Ethernet.init(20);  // Teensy++ 2.0
  //Ethernet.init(15);  // ESP8266 with Adafruit Featherwing Ethernet
  //Ethernet.init(33);  // ESP32 with Adafruit Featherwing Ethernet

  // Open serial communications and wait for port to open:
  Serial.begin(9600);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }

  // start the Ethernet connection:
  Serial.println("Initialize Ethernet with DHCP:");
  if (Ethernet.begin(mac) == 0) {
    Serial.println("Failed to configure Ethernet using DHCP");
    if (Ethernet.hardwareStatus() == EthernetNoHardware) {
      Serial.println("Ethernet shield was not found.  Sorry, can't run without hardware. :(");
    } else if (Ethernet.linkStatus() == LinkOFF) {
      Serial.println("Ethernet cable is not connected.");
    }
    // no point in carrying on, so do nothing forevermore:
    while (true) {
      delay(1);
    }
  }

  Udp.begin(UDPLocalPort);
  
  // start listening for clients
  server.begin();

  Serial.print("Server address:");
  Serial.println(Ethernet.localIP());

  setupTime();
}

void loop() {
  // wait for a new client:
  client = server.available();

  // when the client sends the first byte, say hello:
  if (client) {
    if (!alreadyConnected) {
      // clear out the input buffer:
      client.flush();
      Serial.println("We have a new client");
      client.println("Hello, client!");
      alreadyConnected = true;
    }

    if (client.available() > 0) {
      
      // read the bytes incoming from the client:
      char thisChar = client.read();
      
      if( thisChar != '\n' ) {
        command_buffer += String(thisChar);
      } else {
        executeCommand(command_buffer);
        command_buffer = String("");
      }
    }
  }

  //renew DHCP lease
  Ethernet.maintain();
}

void printCurrentTime() {
  Serial.print(year(now()));
  Serial.print("-");
  Serial.print(month(now()));
  Serial.print("-");
  Serial.print(day(now()));
  Serial.print(" ");
  Serial.print(hour(now()));
  Serial.print(":");
  Serial.print(minute(now()));
  Serial.print(":");
  Serial.println(second(now()));
}

void executeCommand(String command) {
  command = command.substring(0, ( command.length() - 1 ) ); //there's a trailing BR (hex D)
  
  Serial.println("\n EXECUTING COMMAND: ");
  Serial.print(command);

  if( command == String("exit") ) {
    Serial.println(" (Termino sessione)");
    
    alreadyConnected = false;
    client.stop();
  } else if( command.indexOf( "set time" ) != -1 ) {
    Serial.println(" (Set time)");

    int hour = command.substring(9, 11).toInt(); //9 to skip trailing space as well
    int minute = command.substring(12, 14).toInt();

    Alarm.alarmRepeat(hour, minute, 0, wakeup);
  } else if( command.indexOf( "print time" ) != -1 ) {
    Serial.println( " (Print current time)");
     
    printCurrentTime();
  }
}

void wakeup() {
  Serial.println("wakeup!!!!");
}

/*
 * https://www.arduino.cc/en/Tutorial/UdpNtpClient
 */
void setupTime() {
  sendNTPpacket(timeServer); // send an NTP packet to a time server
  
  // wait to see if a reply is available
  delay(1000);
  if (Udp.parsePacket()) {
    // We've received a packet, read the data from it
    Serial.println("NTP packet received...");
    Udp.read(NTPPacketBuffer, NTP_PACKET_SIZE); // read the packet into the buffer

    // the timestamp starts at byte 40 of the received packet and is four bytes,
    // or two words, long. First, extract the two words:

    unsigned long highWord = word(NTPPacketBuffer[40], NTPPacketBuffer[41]);
    unsigned long lowWord = word(NTPPacketBuffer[42], NTPPacketBuffer[43]);
    // combine the four bytes (two words) into a long integer
    // this is NTP time (seconds since Jan 1 1900):
    unsigned long secsSince1900 = highWord << 16 | lowWord;
    Serial.print("Seconds since Jan 1 1900 = ");
    Serial.println(secsSince1900);

    // now convert NTP time into everyday time:
    Serial.print("Unix timestamp = ");
    // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
    const unsigned long seventyYears = 2208988800UL;
    // subtract seventy years:
    time_t epoch = secsSince1900 - seventyYears;
    // print Unix time:
    Serial.println(epoch);

    //Set current time
    setTime(epoch);
    
    Serial.print("UNIX time is ");
    printCurrentTime();

    //Set local time
    adjustTime(3600);

    Serial.print("Local time is ");
    printCurrentTime();
  } else {
    setupTime();
  }
}

// send an NTP request to the time server at the given address
void sendNTPpacket(const char * address) {
  Serial.println("Sending NTP packet...");
  
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
