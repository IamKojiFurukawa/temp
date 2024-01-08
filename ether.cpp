#include <Arduino.h>
#include <ETH.h>
#include <HTTPClient.h>
#include <time.h>

#include <WiFiUdp.h>

#include <WiFiClientSecure.h>
#include <string.h>
#include <esp_task_wdt.h>

#define WT32_ETH
//#define LILYGO_T_INTERNET_POE
//#define LILYGO_T_INTER_COM 

#if   defined(LILYGO_T_INTERNET_POE)
#define ETH_CLK_MODE    ETH_CLOCK_GPIO17_OUT
// Pin# of the enable signal for the external crystal oscillator (-1 to disable for internal APLL source)
#define ETH_POWER_PIN   5 //non connection//
// Type of the Ethernet PHY (LAN8720 or TLK110)
#define ETH_TYPE        ETH_PHY_LAN8720
// I²C-address of Ethernet PHY (0 or 1 for LAN8720, 31 for TLK110)
#define ETH_ADDR        0
// Pin# of the I²C clock signal for the Ethernet PHY
#define ETH_MDC_PIN     23
// Pin# of the I²C IO signal for the Ethernet PHY
#define ETH_MDIO_PIN    18
#define NRST            5
#endif
#ifdef LILYGO_T_INTER_COM //LILYGO T INTER-COM
#define ETH_CLK_MODE    ETH_CLOCK_GPIO0_OUT
// Pin# of the enable signal for the external crystal oscillator (-1 to disable for internal APLL source)
#define ETH_POWER_PIN   4
// Type of the Ethernet PHY (LAN8720 or TLK110)
#define ETH_TYPE        ETH_PHY_LAN8720
// I²C-address of Ethernet PHY (0 or 1 for LAN8720, 31 for TLK110)
#define ETH_ADDR        0
// Pin# of the I²C clock signal for the Ethernet PHY
#define ETH_MDC_PIN     23
// Pin# of the I²C IO signal for the Ethernet PHY
#define ETH_MDIO_PIN    18
#define NRST            5
#endif

#ifdef WT32_ETH
#define NRST            5
#define ETH_ADDR        1
#define ETH_POWER_PIN   16 //-1 //16 // Do not use it, it can cause conflict during the software reset.
#define ETH_POWER_PIN_ALTERNATIVE 16 //17
#define ETH_MDC_PIN    23
#define ETH_MDIO_PIN   18
#define ETH_TYPE       ETH_PHY_LAN8720
#define ETH_CLK_MODE    ETH_CLOCK_GPIO17_OUT // ETH_CLOCK_GPIO0_IN
#endif


HTTPClient http;
static bool eth_connected = false;
static bool eth_done = false;
static bool wifi_done = false;


void WiFiEvent(WiFiEvent_t event)
{
    switch (event) {
    case ARDUINO_EVENT_ETH_START:
        Serial.println("ETH Started");
        // set eth hostname here
        ETH.setHostname("esp32-ethernet");
        break;
    case ARDUINO_EVENT_ETH_CONNECTED:
        Serial.println("ETH Connected");
        break;
    case ARDUINO_EVENT_ETH_GOT_IP:
        Serial.print("ETH MAC: ");
        Serial.print(ETH.macAddress());
        Serial.print(", IPv4: ");
        Serial.print(ETH.localIP());
        if (ETH.fullDuplex()) {
            Serial.print(", FULL_DUPLEX");
        }
        Serial.print(", ");
        Serial.print(ETH.linkSpeed());
        Serial.println("Mbps");
        eth_connected = true;
        break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
        Serial.println("ETH Disconnected");
        eth_connected = false;
        break;
    case ARDUINO_EVENT_ETH_STOP:
        Serial.println("ETH Stopped");
        eth_connected = false;
        break;
    default:
        break;
    }

}

void testClient(const char *host, uint16_t port)
{
    Serial.print("\nconnecting to ");
    Serial.println(host);

    WiFiClient client;
    if (!client.connect(host, port)) {
        Serial.println("connection failed");
        return;
    }
    client.printf("GET / HTTP/1.1\r\nHost: %s\r\n\r\n", host);
    while (client.connected() && !client.available())
        ;
    while (client.available()) {
        Serial.write(client.read());
    }

    Serial.println("closing connection\n");
    client.stop();
}



void Ether_setup()
{
    WiFi.onEvent(WiFiEvent);
#ifdef WT32_ETH
    pinMode(ETH_POWER_PIN_ALTERNATIVE, OUTPUT);
    digitalWrite(ETH_POWER_PIN_ALTERNATIVE, HIGH);
    delay(100);
#else    
    pinMode(NRST, OUTPUT);
    digitalWrite(NRST, 0);
    delay(200);
    digitalWrite(NRST, 1);
    delay(200);
    digitalWrite(NRST, 0);
    delay(200); 
    digitalWrite(NRST, 1);
    delay(200);
#endif
    ETH.begin(ETH_ADDR,
              ETH_POWER_PIN,
              ETH_MDC_PIN,
              ETH_MDIO_PIN,
              ETH_TYPE,
              ETH_CLK_MODE);
    while ( eth_connected != true) delay(1000);

}

// static WiFi   WiFi;
static WiFiUDP wifiUdp;

void httpGet(const char *url, uint8_t *buffer);
int httpsGets(const char *host, int port, const char *page, uint8_t *buffer);


#if 0
/*************************************************
 *   http GET
 *
 **************************************************/
void httpGet(const char *url, char *buffer)
{
	WiFiClient client;
    HTTPClient http;


	http.begin(client, url);
	int httpCode = http.GET();
	String payload = "";
	if (httpCode > 0)
	{
		payload = http.getString();
		Serial.println(httpCode);
		Serial.println(payload);
	}
	else
	{
		Serial.println("Error on HTTP request");
	}
	strcpy(buffer, payload.c_str());
	http.end();
}
#endif



/*********************************************
//curl   "https://homegw-conf.otta.me/update?mac=862785044106519" -H "x-api-key: 9GFvvB7E4MdfldZ36L9y8lLD1VslkPM2" -H "User-Agent: IGS03M-OT/1.03.01"


**********************************************/
int httpsGets(const char *host, int port, const char *page, uint8_t *buffer)
{
	WiFiClientSecure client;

	//  const int TARGET_PORT = 8088;
	// client.setCACert(test_root_ca); // Set the CA certificate for the server of TARGET_HOST
	client.setInsecure();
	if (!client.connect(host, port))
	{
		Serial.println("Connection failed");
		return (-1);
	}
	client.print(String("GET ") + String(page) + " HTTP/1.1\r\n" +
				 "Host: " + String(host) + "\r\n" +
				 //               "accept: application/json\r\n" +
				 "Connection: close\r\n" +
				 "x-api-key: 9GFvvB7E4MdfldZ36L9y8lLD1VslkPM2\r\n" +
				 "User-Agent: IGS03M-OT/1.06.03\r\n" +
				 "\r\n");
	// client.flush();
	String data = "";
	while (client.connected())
	{
		while (client.available())
		{
			String line = client.readStringUntil('\n');
			// Serial.printf("str=%s len=%d \r\n",line.c_str(),line.length());
			if (line == "\r" /*|| line.length()==1 */)
			{
				break;
			}
		}
		while (client.available())
		{
			String line = client.readStringUntil('\n');
			Serial.println(line);
			data += line;
		}
	}
	delay(10);
	client.stop();
	strcpy((char *)buffer, data.c_str());
	return (strlen((char *)buffer));
}

/*******************************************
 * Send UDP payload
 *   retry 3 : timeout 5sec
 *   receve "200" is OK
 *
 *******************************************/
int sendUDP(char *udpAddress, int udpPort, uint8_t *buffer, size_t len)
{
	// const IPAddress udpserver_ip(119, 106, 213, 195);
	int retry = 3;
	int ret = -1;
	int timeout;
	IPAddress remoteIP; // 相手のIPアドレス
	int remotePort;
	// Serial.printf("SendUDP\n");
	wifiUdp.beginPacket(udpAddress, udpPort);
	wifiUdp.setTimeout(1000); // send 1秒のタイムアウトを設定udpserver_ip ,
	while (retry--)
	{
		// 送信
		ret = wifiUdp.write(buffer, len);
		wifiUdp.endPacket();
		// Serial.printf("SendUDP wr=%d\n",ret);
		//  受信
		timeout = 30; // 200*30  6sec
		int packetSize = wifiUdp.parsePacket();
		// Serial.printf("recv UDP sz=%d\n",packetSize);
		while (--timeout)
		{
			if (packetSize = wifiUdp.parsePacket())
				break;
			delay(200);
		}
		// Serial.printf("recv UDP sz=%d\n",packetSize);
		if (packetSize)
		{
			char data[20];
			int rcvlen = wifiUdp.read(data, 20);
			remoteIP = wifiUdp.remoteIP();
			remotePort = wifiUdp.remotePort();
			// Serial.print(remoteIP);
			data[rcvlen] = 0;
			Serial.printf(" UDP  recv[%s]\n", data);
			if (data[0] == '2' && data[1] == '0' && data[2] == '0')
			{
				ret = 0;
				break;
			}
			Serial.printf("Send len=%d  %02x %02x %02x %02x %02x %02x\n", len, buffer[0], buffer[1], buffer[2], buffer[3], buffer[4], buffer[5]);
			delay(20);
		}
		esp_task_wdt_reset();
	}
	if (retry <= 0)
		Serial.printf("Send timeout\n");
	return (ret);
}

