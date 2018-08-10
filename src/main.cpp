// V1.2 20180809 - converted to PlatformIO
#include <Arduino.h>
#include <SPI.h>
#include <Ethernet.h>
#include "Adafruit_MQTT.h"
#include "Adafruit_MQTT_Client.h"
#include <EthernetClient.h>
#include <Dns.h>
#include <Dhcp.h>

#include "RemoteReceiver.h"
#include "RemoteTransmitter.h"
#include "NewRemoteReceiver.h"
#include "NewRemoteTransmitter.h"
#include "InterruptChain.h"
#include <avr/wdt.h>

/************************* MQTT Setup *********************************/
#define AIO_SERVER "[SERVER]"
#define AIO_SERVERPORT 1883
#define AIO_USERNAME "[USER]"
#define AIO_KEY "[PASSWORD]"

/****************************** Feeds ***************************************/
byte mac[] = {0xDE, 0xED, 0xBA, 0xFE, 0xFE, 0xED};
EthernetClient client;
Adafruit_MQTT_Client mqtt(&client, AIO_SERVER, AIO_SERVERPORT, AIO_USERNAME, AIO_KEY);
Adafruit_MQTT_Subscribe kakusend = Adafruit_MQTT_Subscribe(&mqtt, "kaku/send");
Adafruit_MQTT_Subscribe newkakusend = Adafruit_MQTT_Subscribe(&mqtt, "newkaku/send");

Adafruit_MQTT_Publish kaku = Adafruit_MQTT_Publish(&mqtt, "kaku/receive");
Adafruit_MQTT_Publish newkaku = Adafruit_MQTT_Publish(&mqtt, "newkaku/receive");
Adafruit_MQTT_Publish MQTTstatus = Adafruit_MQTT_Publish(&mqtt, "MQTT/status");

/*********************** RF 433HMZ SETUP *******************************/
#define PIN_RF_TX_VCC 6  // +5 volt / Vcc transmitter
#define PIN_RF_TX_DATA 5 // Data  433Mhz transmitter
#define PIN_RF_RX_VCC 4  // +5 volt / Vcc receiver
#define PIN_RF_RX_DATA 3 // Data  433Mhz receiver

KaKuTransmitter kaKuTransmitter(PIN_RF_TX_DATA);

int retry = 0;
int MQTT = 30;
int MQTTcount = 0;

void showOldCode(unsigned long receivedCode, unsigned int period);
void showNewCode(NewRemoteCode receivedCode);
void MQTT_connect();

/*************************** Sketch Code ************************************/

void setup()
{
  Serial.begin(115200);
  delay(10);
  Serial.println(F("KAKU MQTT gateway v1.2"));
  Serial.println(F("Waiting for IP..."));

  if (Ethernet.begin(mac))
  {
    Serial.print("IP = ");
    Serial.println(Ethernet.localIP());
  }
  else
    Serial.print("NO CONNECTION TO LAN");
  delay(1000); // give ethernet a sec
  mqtt.subscribe(&kakusend);
  mqtt.subscribe(&newkakusend);

  // Interrupt -1 to indicate you will call the interrupt handler with InterruptChain
  RemoteReceiver::init(-1, 1, showOldCode);
  // Again, interrupt -1 to indicate you will call the interrupt handler with InterruptChain
  NewRemoteReceiver::init(-1, 1, showNewCode);
  //interrupt 4 (pin 19) for Mega, 1 (pin3) for uno
  // Set interrupt mode CHANGE, instead of the default LOW.
  InterruptChain::setMode(1, CHANGE);
  // On interrupt, call the interrupt handlers of remote and sensor receivers
  InterruptChain::addInterruptCallback(1, RemoteReceiver::interruptHandler);
  InterruptChain::addInterruptCallback(1, NewRemoteReceiver::interruptHandler);

  Serial.println(F("KAKU MQTT gateway online!"));

  delay(1000);

  pinMode(PIN_RF_TX_VCC, OUTPUT);
  pinMode(PIN_RF_RX_VCC, OUTPUT);
  digitalWrite(PIN_RF_RX_VCC, HIGH);
  digitalWrite(PIN_RF_TX_VCC, LOW); // turn off power to transmitter to prvvent interference

  wdt_enable(WDTO_8S);
}

void loop()
{
  wdt_reset();
  MQTT_connect();

  if (MQTT >= 30)
  {
    //MQTTstatus.publish(millis()/1000);
    MQTTcount++;
    MQTT = 0;

    Serial.print(millis());
    Serial.print(": status: ");
    if (!mqtt.ping())
    {
      // MQTT pings failed, lets reconnect
      //  mqtt.disconnect();
      Serial.println("Ping fail!");
    }
    else
    {
      Serial.println(F("OK!"));
    }
  }
  MQTT++;

  // this is our 'wait for incoming subscription packets' busy subloop
  Adafruit_MQTT_Subscribe *subscription;
  while ((subscription = mqtt.readSubscription(1000)))
  {

    // Klik AAN klik UIT
    if (subscription == &kakusend)
    {
      Serial.print(F("Transmitting:  "));
      //Serial.println((char *)kakusend.lastread);
      digitalWrite(PIN_RF_RX_VCC, LOW); // turn of receiver
      digitalWrite(PIN_RF_TX_VCC, HIGH);
      wdt_reset();
      //send data
      unsigned long code = atol((char *)kakusend.lastread);
      Serial.println(code);
      RemoteTransmitter::sendCode(PIN_RF_TX_DATA, code, 354, 4);
      wdt_reset();

      digitalWrite(PIN_RF_RX_VCC, HIGH); // turn on receiver
      digitalWrite(PIN_RF_TX_VCC, LOW);
    }

    // New KaKu
    if (subscription == &newkakusend)
    {
      Serial.print(F("Transmitting: "));
      Serial.println((char *)newkakusend.lastread);
      digitalWrite(PIN_RF_RX_VCC, LOW); // turn of receiver
      digitalWrite(PIN_RF_TX_VCC, HIGH);
      wdt_reset();

      String myString = (char *)newkakusend.lastread;
      int commaIndex = myString.indexOf(',');
      //  Search for the next comma just after the first
      int secondCommaIndex = myString.indexOf(',', commaIndex + 1);
      String firstValue = myString.substring(0, commaIndex);
      String secondValue = myString.substring(commaIndex + 1, secondCommaIndex);
      String thirdValue = myString.substring(secondCommaIndex + 1); // To the end of the string

      long addr = firstValue.toInt();
      int unit = secondValue.toInt();
      boolean state = thirdValue.equals("true");
      int dim = thirdValue.toInt();

      Serial.print("addr: ");
      Serial.print(addr);
      Serial.print(" unit: ");
      Serial.print(unit);
      Serial.print(" state: ");
      Serial.println(thirdValue);

      NewRemoteTransmitter transmitter(addr, PIN_RF_TX_DATA, 260, 4);

      if (thirdValue.equals("true") || thirdValue.equals("false"))
      {
        if (unit == 99)
          transmitter.sendGroup(state); //was fout, altijd naar groep (2015.12.10)
        else
          transmitter.sendUnit(unit, state);
      }
      else
        transmitter.sendDim(unit, dim);
      wdt_reset();
      digitalWrite(PIN_RF_RX_VCC, HIGH); // turn on receiver
      digitalWrite(PIN_RF_TX_VCC, LOW);
    }
  }
}

// Function to connect and reconnect as necessary to the MQTT server.
// Should be called in the loop function and it will take care if connecting.
void MQTT_connect()
{
  int8_t ret;

  // Stop if already connected.
  if (mqtt.connected())
  {
    return;
  }

  Serial.print("Connecting to MQTT... ");

  while ((ret = mqtt.connect()) != 0)
  { // connect will return 0 for connected
    Serial.println(mqtt.connectErrorString(ret));
    Serial.println("Retrying MQTT connection in 5 seconds...");
    wdt_reset();
    mqtt.disconnect();
    delay(5000);                 // wait 5 seconds
    void (*resetFunc)(void) = 0; //declare reset function at address 0
    if (retry >= 2)
      resetFunc(); //call reset
    retry++;
  }
  Serial.println("MQTT Connected!");
}

// shows the received code sent from an old-style remote switch
void showOldCode(unsigned long receivedCode, unsigned int period)
{
  wdt_reset();
  // Print the received code.
  Serial.print("Code: ");
  Serial.print(receivedCode);
  Serial.print(", period: ");
  Serial.print(period);
  Serial.println("us.");

  char *data;
  char message_buffer[100];
  data = dtostrf(receivedCode, 1, 0, message_buffer);

  if (!kaku.publish(data))
  {
    Serial.println(F("Failed"));
  }
  else
  {
    Serial.println(F("OK!"));
  }
}

// Shows the received code sent from an new-style remote switch
void showNewCode(NewRemoteCode receivedCode)
{
  wdt_reset();
  // Print the received code.
  Serial.print("Addr ");
  Serial.print(receivedCode.address);

  if (receivedCode.groupBit)
  {
    Serial.print(" group");
    receivedCode.unit = 99;
  }
  else
  {
    Serial.print(" unit ");
    Serial.print(receivedCode.unit);
  }

  switch (receivedCode.switchType)
  {
  case NewRemoteCode::off:
    Serial.print(" off");
    break;
  case NewRemoteCode::on:
    Serial.print(" on");
    break;
  case NewRemoteCode::dim:
    Serial.print(" dim");
    Serial.print(receivedCode.dimLevel);
    break;
  }

  if (receivedCode.dimLevelPresent)
  {
    Serial.print(", dim level ");
    Serial.print(receivedCode.dimLevel);
  }

  Serial.print(", period: ");
  Serial.print(receivedCode.period);
  //Serial.print("{\"addres\":" + receivedCode.period + ", \"unit\":" + receivedCode.unit +"}");
  Serial.println("us.");

  String RECEIVE_NEWKAKU = "{\"addr\":\"";
  RECEIVE_NEWKAKU = RECEIVE_NEWKAKU + receivedCode.address + "\",\"unit\":\"" + receivedCode.unit;

  switch (receivedCode.switchType)
  {
  case NewRemoteCode::off:
    RECEIVE_NEWKAKU = RECEIVE_NEWKAKU + "\",\"state\":\"false\"";
    break;
  case NewRemoteCode::on:
    RECEIVE_NEWKAKU = RECEIVE_NEWKAKU + "\",\"state\":\"true\"";
    break;
  case NewRemoteCode::dim:
    RECEIVE_NEWKAKU = RECEIVE_NEWKAKU + "\",\"dim\":\"" + receivedCode.dimLevel;
    break;
  }

  RECEIVE_NEWKAKU = RECEIVE_NEWKAKU + "}";

  char BUF[128];

  RECEIVE_NEWKAKU.toCharArray(BUF, 128);

  if (!newkaku.publish(BUF))
  {
    Serial.println(F("Failed"));
  }
  else
  {
    Serial.println(F("OK!"));
  }
}