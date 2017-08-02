// Скетч для ESP8266,
//  считывает показания с MH-z19 (по UART), DHT22
//  выводит концентрацию CO2 на дисплей типа http://www.arduitronics.com/product/220/seven-segment-4-digit-displaycatalex
//  кидает температуру, влажность и СО2 на thingspeak.com

/* Соединения
DHT22:
  1 - 3.3v
  2 - D7
  3 - NC
  4 - GND

Display TM1637:
  CLK - D6
  DIO - D5
  VCC - 3.3v
  GND - GND

Mh-z19
  Tx - D1
  Rx - D2
  Vin - Vin
  GND - GND
*/

#include <DHT.h>
#include <SoftwareSerial.h>
#include <TM1637Display.h>
#include <ESP8266WiFi.h>
#include <Adafruit_NeoPixel.h>
#ifdef __AVR__
  #include <avr/power.h>
#endif


#define BLUE_LED_PIN 2  // синий и красный LED на плате ESP8266
#define RED_LED_PIN 16  //
#define MHZ19_RX_PIN  5 // подключение Mh-z19
#define MHZ19_TX_PIN  4 //
#define DHTPIN 13        // what pin we're connected to
#define CLK 12          // подключение дисплея
#define DIO 14          //
#define WS8212_PIN 15   // ws8212

// replace with your channel's thingspeak API key,
String apiKey = "J6P3LU0ANLWHYI6D";
const char* ssid = "SSID";	   // your WiFi SSID
const char* password = "PASS"; // your wiFi password

const char* server = "api.thingspeak.com";

// количество светодиодов WS8212 в ленте
const int led_num = 5;

const bool wifi_on = true;
const bool display_on = true;
const bool blue_led_on = false;
const bool dht22_on = true;

DHT dht;

SoftwareSerial Mhz19_serial(MHZ19_RX_PIN, MHZ19_TX_PIN); // RX, TX сенсора

TM1637Display display(CLK, DIO);

WiFiClient client;

Adafruit_NeoPixel LED_strip = Adafruit_NeoPixel(5, WS8212_PIN, NEO_GRB + NEO_KHZ800);


void setup()
{
  Serial.begin(115200);
  delay(10);

  if (display_on)
  {
    uint8_t data[] = { 0xff, 0xff, 0xff, 0xff };
    display.setBrightness(0x0f);

    // All segments on
    display.setSegments(data);
    display.showNumberDec( 0, false);
  }

  if (dht22_on)
    dht.setup(DHTPIN);

  Mhz19_serial.begin(9600); //программный UART-порт для общения с датчиком Mh-z19

  pinMode(BLUE_LED_PIN, OUTPUT);

  LED_strip.begin();
  LED_strip.show(); // Initialize all pixels to 'off'

}


void loop()
{

  float h = 0.0;
  float t = 0.0;
  unsigned int ppm = 0;

  bool h_freshed = false;
  bool t_freshed = false;
  bool ppm_freshed = false;

  bool some_data_refreshed = false;

  if (dht22_on)
  {
    h = dht.getHumidity();
    t = dht.getTemperature();

    if (isnan(h) || isnan(t))
    {
      Serial.println("Failed to read from DHT sensor!");
      return;
    }
    else
      some_data_refreshed = true;
  }


  // команда на считывание данных из Mh-z19
  Serial.print("Mh-z19 reading...");
  byte mhz19_co2_request_command[9] = {0xFF, 0x01, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x79};
  Mhz19_serial.write( mhz19_co2_request_command, 9);//запрос PPM CO2
  unsigned char response[9]; // здесь будет ответ от Mh-z19
  if ((Mhz19_serial.readBytes(response, 9) == 9) || (response[1] == 0x86))
  {
    // рассчитываем checksum
    byte checksum = 0;
    for (byte i = 1; i < 8; i++)
      checksum += response[i];
    checksum = 255 - checksum;
    checksum ++;

    if (checksum == response[8])
    {
      Serial.println("OK");
      unsigned int responseHigh = (int) response[2];
      unsigned int responseLow = (int) response[3];

      // ну и по мануалу из ответа считаем PPM
      ppm = (256 * responseHigh) + responseLow;
      ppm_freshed = true;
      some_data_refreshed = true;

      if (display_on)
        display.showNumberDec( ppm, false);

      uint32_t color = LED_strip.Color(0, 255, 0);

      if (ppm < 600)
        color = LED_strip.Color(0, 32, 0);
      else if (ppm < 800)
        color = LED_strip.Color(32, 32, 0); 
      else if (ppm < 1000)
        color = LED_strip.Color(32, 12, 0); 
      else
        color = LED_strip.Color(32, 0, 0); 

      for (int i=0; i<led_num; i++)
        LED_strip.setPixelColor(i, color);
      LED_strip.show();

    }
    else
      Serial.println("FAILED - wrong checksum");
  }
  else
    Serial.println("FAILED - wrong responce");

  if (wifi_on && some_data_refreshed)
  {
    if (blue_led_on)
      digitalWrite(BLUE_LED_PIN, LOW);

    Serial.println();
    Serial.println();
    Serial.print("Connecting to ");
    Serial.print(ssid);
    Serial.print("...");

    WiFi.begin(ssid, password);

    if (WiFi.status() == WL_CONNECTED)
    {
      Serial.println("OK");
      Serial.print("Connecting to ");
      Serial.print(server);
      Serial.print("...");

      if (client.connect(server, 80))
      { //   "184.106.153.149" or api.thingspeak.com
        Serial.println("OK");

        String postStr = apiKey;
        postStr += "&field1=";
        postStr += String(t);
        postStr += "&field2=";
        postStr += String(h);

        if (ppm_freshed)
        {
          postStr += "&field3=";
          postStr += String(ppm);
        }

        postStr += "\r\n\r\n";

        client.print("POST /update HTTP/1.1\n");
        client.print("Host: api.thingspeak.com\n");
        client.print("Connection: close\n");
        client.print("X-THINGSPEAKAPIKEY: " + apiKey + "\n");
        client.print("Content-Type: application/x-www-form-urlencoded\n");
        client.print("Content-Length: ");
        client.print(postStr.length());
        client.print("\n\n");
        client.print(postStr);

        if (blue_led_on)
          digitalWrite(BLUE_LED_PIN, HIGH);

        Serial.print("Temperature: ");
        Serial.print(t);
        Serial.print("C Humidity: ");
        Serial.print(h);
        Serial.print("% CO2: ");
        Serial.print(ppm);
        Serial.print("ppm");
        Serial.print("\n");

        client.stop();
      }
      else
        Serial.println("FAILED");

      WiFi.disconnect();
    }
    else
      Serial.println("FAILED");

  }

  Serial.println("Waiting...");
  // thingspeak needs minimum 15 sec delay between updates
  delay(20000);
}

