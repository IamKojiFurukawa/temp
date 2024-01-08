#include <Arduino.h>
/*********************************************
 * ESP32 iBeacon scan
 *
 *
 * addr=000b9439227d rssi=-100 uuid=1A49BD5B-3ADC-43B7-B42F-1ECA1A931649,major=55,minor=9606,power=-56
 *********************************************/
#include "BLEDevice.h"
#include "BLEUtils.h"
#include "BLEScan.h"
#include "BLEAdvertisedDevice.h"
#include <time.h>
// #include <WiFi.h>
// #include <WiFiUdp.h>
// #include <HTTPClient.h>
// #include <WiFiClientSecure.h>
/// #include <TimeLib.h>
#include <string.h>
#include <EEPROM.h>
#include <esp_sntp.h>
#include <esp_task_wdt.h>
#include <ArduinoJson.h>
#include <string.h>
#include "mbedtls/aes.h"
#include <esp_task_wdt.h>

#define DIFF_JST_GMT (3600 * 9)
// #define PIN_TX                  27
// #define PIN_RX                  26
// #define UART_BAUD               9600
// #define PIN_DTR                 25
// #define PWR_PIN                 4
#define LED_PIN 12
// #define POWER_PIN               4
// #define IND_PIN                 36
// #define PIR_PIN                 36

#define PATBT_PIN 36 // input button
#define PATL_PIN 0	 // Patlight ON/Off

//#define USE_LTE // LTE or Wi-Fi

BLEScan *pBLEScan;

typedef struct uuidlst
{
	uint8_t uuid[16];
	uint8_t enable; // uuid type numbrt 0:disable
} uuidlst_t;

typedef struct server_setting
{
	uint16_t id; // 0x3aa3 ROM save flag
	char ntpServerName[40];
	char host_addr[40];
	int host_port;
	int ivl;
	char hb_addr[40];
	int hb_port;
	int hb_ivl;
	char firm_v[20];
	char firmup_addr[100];
	int bdb;
	int bdb_rst;
	int gw_rb;
	int pato_ivl;
	int pato_RSI;
} server_setting_t;

// relay sever conf  replace after http
static server_setting_t server_set;
/* = {
	.ntpServerName = {"pool.ntp.org"},
	.host_addr = {"119.106.213.195"},
	.host_port = 4445,
	.ivl = 60,
	.hb_addr = {"119.106.213.195"},
	.hb_port = 4445,
	.hb_ivl = 600,
	.firm_v = {"1.06.03"}
}; */

// config server
#if 1
const char *CONF_HOST = "kd119106213195.ppp-bb.dion.ne.jp";
#ifdef USE_LTE
const char *CONF_PAGE = "/update";
#else
const char *CONF_PAGE = "https://kd119106213195.ppp-bb.dion.ne.jp/update";
#endif
const int CONF_PORT = 443;
//  const String TARGET_PAGE = "http://kd119106213195.ppp-bb.dion.ne.jp/update";
#else
const char *CONF_HOST = "homegw-conf.otta.me";
const char *CONF_PAGE = "https://homegw-conf.otta.me/update?mac=862785044106519";
const int CONF_PORT = 443;
#endif

/* otta Beacond DB */
typedef struct OTTA_BEACONS
{
	uint32_t lasttime;
	uint32_t pt_lasttime;
	uint8_t uuidtype;
	uint8_t count;
	int16_t sumRSSI; // Average
	int8_t maxRSSI;
	int8_t txpower;
	//   uint8_t  proxi;//not calk
	uint16_t major;
	uint16_t minor;
	//   int16_t  proximity;
} OTTA_BEACONS_t;

/* otta Pat_light Beacond DB */
typedef struct OTTA_PAT_BEACONS
{
	uint32_t lasttime;
	uint16_t major;
	uint16_t minor;
	uint8_t uuidtype;
	int8_t rssi;
} OTTA_PAT_BEACONS_t;

/* NTP SEREVER */
const char *ntpServer1 = "pool.ntp.org";
const char *ntpServer2 = "time.nist.gov";
#define JST (3600 * 9)

// config relay conf url
#define CONFIGURL1 "https://kd119106213195.ppp-bb.dion.ne.jp/update"
#define CONFIGURL2 "https://homegw-conf.otta.me/update?mac=862785044106519"
#define CONFIGURL3 "http://kd119106213195.ppp-bb.dion.ne.jp:8088/update"

const char FirmVersion[18] = {"IGS03M-OT/1.06.03"}; // use for Heart Beat
int LTErssi = 56;									// dummy
char imei_str[16] = {"862785044113366"};			// LTE replace by Modem ID`
char imsi_str[16] = {"440103147265967"};
unsigned long long imei;
bool gotNTP;

#define MAX_BEACON_COUNT 240
// otta beacon data base
static OTTA_BEACONS_t beacos[MAX_BEACON_COUNT + 2] = {0};
static OTTA_PAT_BEACONS_t pat_beacos[MAX_BEACON_COUNT + 2] = {0};
// otta UUID convert  table
#define MAX_UUIDS 10
uuidlst_t uuids[MAX_UUIDS] = {0};
/*
	{	{0x44, 0x57, 0x61, 0x22, 0x37, 0x18, 0x4b, 0x96, 0xbc, 0x85, 0xcb, 0x1c, 0x44, 0xac, 0xbb, 0x0b},2,	},
	{	{0x00, 0x00, 0x00, 0x00, 0xA0, 0x33, 0x10, 0x01, 0xB0, 0x00, 0x00, 0x1C, 0x4D, 0xE7, 0x5C, 0xE5},1, },
	{	{0x1a, 0x49, 0xbd, 0x5b, 0x3a, 0xdc, 0x43, 0xb7, 0xb4, 0x2f, 0x1e, 0xca, 0x1a, 0x93, 0x16, 0x49},3, },
	{	{0xe9, 0x6f, 0x92, 0xf5, 0xc2, 0xf9, 0x4b, 0xd4, 0x91, 0x43, 0x2d, 0x32, 0x5e, 0x7e, 0xb3, 0xd0},4, },
	{	{0x5E, 0x34, 0xD5, 0xD0, 0xA0, 0x3B, 0x4D, 0xB8, 0xA9, 0xCB, 0xCE, 0xDC, 0x02, 0x12, 0xD8, 0x95},5, },
	{	{0x42, 0xE4, 0x4D, 0x1B, 0xE2, 0x3F, 0x48, 0x81, 0x8B, 0xF0, 0x37, 0xB6, 0xCA, 0x4D, 0xA7, 0x60},6, }
}; */


static uint8_t sendbuffer[MAX_BEACON_COUNT * 11 + 30];
static unsigned char cipherTextOutput[MAX_BEACON_COUNT * 11 + 46];


int get_json_data(char *buffer);
void httpGet(const char *url, uint8_t *buffer);
int httpsGets(const char *host, int port, const char *page, uint8_t *buffer);
unsigned long long parseInt(const char *str);
int encryptUDP(char *pimsi, char *pimei, int payloadlen, unsigned char *payloadIN, unsigned char *payloadOUT);
int decryptUDP(char *pimsi, char *pimei, int payloadlen, unsigned char *payloadIN, unsigned char *payloadOUT);
int main_loop(void);
void threadFunction(void *pvParameters);
static void restore_EEPROM();
static void save_EEPROM();
TaskHandle_t th;
/// LTE Modem //
#ifdef USE_LTE
void flushSerailRbuffer();
int waitATOK();
void netSetup();
int wait_registration();
void modem_reset();
void modem_on();
void modem_off();
void modem_sleep();
void modem_wake();
void shutdown();
void wait_till_ready();
int lteSetup();
int getSIMINFO(char *imei, char *imsi);
int sendUDPLTE(char *udpAddress, int udpPort, uint8_t *buffer, size_t len);
int httpsGetLTE(const char *host, int port, const char *page, uint8_t *buffer);
int httpGetLTE(const char *host, int port, const char *page, uint8_t *buffer);
int GetTimeLTE();
#endif

#define USE_ETHER
#ifdef USE_ETHER
//Ether_setup
void Ether_setup();
#else
/// wi-fi
void wifiSetup();
#endif

//network protocol
void httpGet(const char *url, char *buffer);
int httpsGets(const char *host, int port, const char *page, uint8_t *buffer);
int sendUDP(char *udpAddress, int udpPort, uint8_t *buffer, size_t len);

//GNSS
//int enableGPS();
//int disableGPS();
//bool getLoaction();
//////////////// LTE ////////

/*********************************************
 UART Seral から1行分の文字列入力
 　エコーバックを行う
**********************************************/
void console_input(char *str) // ポインターで引数渡される
{
	int c;
	*str = 0; // 文字列のNULL終端コード 00を入れる
	while (1)
	{
		if (Serial.available())
		{
			c = Serial.read(); // UART Console から１文字入力
			if (c == 0x0d || c == 0x0a)
			{			  // 改行コード CR or LF か ?
				*str = 0; // 文字列のNULL終端コード 00を入れる CR LFは含まない
				break;	  // ループを抜け終了
			}
			*str++ = c;		 //`文字列保存
			Serial.write(c); // エコーバック
		}
	}
}

/*************************************
 *  string to 64bit int
 **************************************/
unsigned long long parseInt(const char *str)
{
	unsigned long long value = 0;
	for (int i = 0; i < strlen(str); i++)
	{
		value = value * 10 + str[i] - '0';
	}
	return value;
}

/***************************************************
  check UUID type 0-13
  if not found -1
****************************************************/
static int getUUIDtype(uint8_t *p_uuid)
{
	int i;
	for (i = 0; i < MAX_UUIDS; i++)
	{
		if (uuids[i].enable)
		{
			if (memcmp(p_uuid, uuids[i].uuid, 16) == 0) // comp UUID
				return (uuids[i].enable);
		}
	}
	return (-1);
}

/******************************************
   Clear Beacon DB
******************************************/
void ClearBeaconDB()
{
	// disable int
	noInterrupts();
	memset(beacos, 0, sizeof(beacos));
	interrupts();
	// enable int
}

/******************************************
   put  Beacon data to DB return 1:new  0:already
   over flow
******************************************/
static int putBeaconDB(uint8_t uuidtype, uint32_t major, uint32_t minor, int8_t rssi, int8_t txpower, time_t now)
{
	int i;
	for (i = 0; i < MAX_BEACON_COUNT; i++)
	{
		if (beacos[i].uuidtype == uuidtype &&
			beacos[i].major == major && beacos[i].minor == minor)
		{
			// beacon found, update beacon data
			beacos[i].lasttime = now;
			beacos[i].count++;
			beacos[i].sumRSSI += rssi;
			if (beacos[i].maxRSSI < rssi)
				beacos[i].maxRSSI = rssi;
			return 0;
			// break;
		}
	}
	if (i >= MAX_BEACON_COUNT)
	{
		// new beacon
		for (i = 0; i < MAX_BEACON_COUNT; i++)
			if (beacos[i].count == 0)
				break;
		if (i >= MAX_BEACON_COUNT)
		{
			// out ouf buffer
			return 2;
		}
		else
		{
			// new beacon found
			// if (i>130) i=130;
			beacos[i].lasttime = now;
			beacos[i].count = 1;
			beacos[i].maxRSSI = beacos[i].sumRSSI = rssi;
			beacos[i].uuidtype = uuidtype;
			beacos[i].major = major;
			beacos[i].minor = minor;
			beacos[i].txpower = txpower;
			return 1; // new
		}
	}
	return -1;
}

/******************************************
   put Pat light Beacon data to DB return 1:new  0:already
   over flow
******************************************/
static int putPatBeaconDB(uint8_t uuidtype, uint32_t major, uint32_t minor, int8_t rssi,time_t now)
{
	int i;
	time_t mostoldtm = 0xffffffff;
	int oldidx = MAX_BEACON_COUNT;
	major &= 0x7fff; // ignor battery bit
	for (i = 0; i < MAX_BEACON_COUNT; i++)
	{
		if (pat_beacos[i].lasttime <= mostoldtm)
		{
			mostoldtm = pat_beacos[i].lasttime;
			oldidx = i;
		}
		if (pat_beacos[i].uuidtype == uuidtype &&
			pat_beacos[i].major == major && pat_beacos[i].minor == minor)
		{
			// found update beacon data
			pat_beacos[i].lasttime = now;
			pat_beacos[i].rssi = rssi;
			return 0;
		}
		if (pat_beacos[i].major == 0 && pat_beacos[i].minor == 0 && rssi >= server_set.pato_RSI)
		{
			// add new beacon
			pat_beacos[i].lasttime = now;
			pat_beacos[i].uuidtype = uuidtype;
			pat_beacos[i].major = major;
			pat_beacos[i].minor = minor;
			pat_beacos[i].rssi = rssi;
			return 1; // new
		}
	}
	// あふれたときは古いのを上書き
	pat_beacos[oldidx].lasttime = now;
	pat_beacos[oldidx].uuidtype = uuidtype;
	pat_beacos[oldidx].major = major;
	pat_beacos[oldidx].minor = minor;
	pat_beacos[oldidx].rssi = rssi;
	return oldidx;
}

#define SENDTYPE 5	 // Beacon Ingix
#define SENDHBTYPE 6 // Beacon Ingix HB
/*************************************************
 Make payload for Udp
 240
 max 120 beacon par packet
 bcindex -1  heart bert

*************************************************/
int makeBCPayload(uint8_t *sendbuf, int bcindex)
{
	int i, j;
	int numBeacon;
	int maxBeacon;
	uint16_t prx = 0;
	uint16_t maxrsi;
	int rssiav = 0;
	int txpow = 0;
	float tm;
	int bodysize;

	for (numBeacon = bcindex; numBeacon < MAX_BEACON_COUNT; numBeacon++)
		if (beacos[numBeacon].count == 0)
			break;
	maxBeacon = numBeacon;
	numBeacon -= bcindex; // this time send
	if (numBeacon > 120)
		numBeacon = 120;

	bodysize = 13 + numBeacon * 11;
	sendbuf[0] = bodysize >> 8;
	sendbuf[1] = bodysize & 0xFF;
	sendbuf[2] = SENDTYPE;		 // Beacon type
	sendbuf[3] = numBeacon >> 8; // number of device
	sendbuf[4] = numBeacon;
	sendbuf[5] = imei >> 56; // 64bit IMEI
	sendbuf[6] = imei >> 48;
	sendbuf[7] = imei >> 40;
	sendbuf[8] = imei >> 32;
	sendbuf[9] = imei >> 24;
	sendbuf[10] = imei >> 16;
	sendbuf[11] = imei >> 8;
	sendbuf[12] = imei;
	j = 13; // # header size
	if (bcindex >= 0)
	{
		for (i = bcindex; i < (bcindex + numBeacon); i++)
		{
			time_t epoctime = beacos[i].lasttime - DIFF_JST_GMT;
			rssiav = beacos[i].sumRSSI / beacos[i].count;
			maxrsi = (beacos[i].maxRSSI > 0) ? 0 : -beacos[i].maxRSSI;
			if (maxrsi > 127)
				maxrsi = 127;
			float tm = (float)(beacos[i].txpower - rssiav) / 20.0f;
			prx = (int)(pow(10.0f, tm) + 0.5f);
			if (prx > 255)
				prx = 255;
			Serial.printf("id=%d maj=%5d min=%5d a.prx=%2dm a.RS=%ddB\n", beacos[i].uuidtype, beacos[i].major, beacos[i].minor, prx, rssiav);
			sendbuf[j++] = beacos[i].uuidtype << 4 | beacos[i].major >> 12;
			sendbuf[j++] = beacos[i].major >> 4 & 0xFF;
			sendbuf[j++] = (beacos[i].major & 0x0F) << 4 | beacos[i].minor >> 12;
			sendbuf[j++] = (beacos[i].minor & 0x0ff0) >> 4;
			sendbuf[j++] = (beacos[i].minor & 0x0f) << 4 | prx >> 4;
			sendbuf[j++] = (prx & 0x0f) << 4 | maxrsi >> 3;
			sendbuf[j++] = (maxrsi & 0x07) << 5 | epoctime >> 27;
			sendbuf[j++] = epoctime >> 19 & 0xff;
			sendbuf[j++] = epoctime >> 11 & 0xff;
			sendbuf[j++] = epoctime >> 3 & 0xff;
			sendbuf[j++] = epoctime << 5 & 0xff;
			/// beacos[i].sumRSSI = beacos[i].count = 0;
		}
	}
	else
	{ // Heart Beat
		j = bodysize = 30;
		int ltetype = 2;
		sendbuf[0] = bodysize >> 8;
		sendbuf[1] = bodysize & 0xff;
		sendbuf[2] = SENDHBTYPE; // # HeartBeat Ingix type
		sendbuf[3] = imei >> 56;
		sendbuf[4] = imei >> 48;
		sendbuf[5] = imei >> 40;
		sendbuf[6] = imei >> 32;
		sendbuf[7] = imei >> 24;
		sendbuf[8] = imei >> 16;
		sendbuf[9] = imei >> 8;
		sendbuf[10] = imei;
		memcpy(&sendbuf[11], FirmVersion, 17);
		sendbuf[28] = ltetype << 4 | LTErssi >> 4;
		sendbuf[29] = (LTErssi & 0x0f) << 4 | 0; // # err=0
	}
	return (j);
}

/************************************************
 * BLE beacon scan call back
 ***********************************************/
class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks
{
	void onResult(BLEAdvertisedDevice advertisedDevice)
	{
		// アドバタイジングデータを受け取った
		// Serial.println(advertisedDevice.toString().c_str());
		// char txuartbuf[80];
		// char uuid[60];
		// char BDaddr[20];
		// char BDaddrnv[20];

		// BLEAddress addr = advertisedDevice.getAddress();
		// strcpy(BDaddr, addr.toString().c_str());
		// sprintf(BDaddrnv, "%c%c%c%c%c%c%c%c%c%c%c%c", BDaddr[0], BDaddr[1], BDaddr[3], BDaddr[4], BDaddr[6], BDaddr[7], BDaddr[9], BDaddr[10], BDaddr[12], BDaddr[13], BDaddr[15], BDaddr[16]);
        time_t now = time(NULL);
		int rssi = advertisedDevice.getRSSI();
		std::string data = advertisedDevice.getManufacturerData();

		if (data.length() == 25)
		{
			if ((data[0] == 0x4c) && (data[1] == 0x00) && (data[2] == 0x02) && (data[3] == 0x15))
			{ // iBeacon
				int uuidtyp = getUUIDtype((uint8_t *)data.c_str() + 4);
				if (uuidtyp > 0)
				{ // Otta beacon
					int major = (int)(((data[20] & 0xff) << 8) + (data[21] & 0xff));
					int minor = (int)(((data[22] & 0xff) << 8) + (data[23] & 0xff));
					signed char power = (signed char)(data[24] & 0xff);
					// float tm = (float)(power - rssi) / 20.0f;
					// int prx = (int)(pow(10.0, tm) + 0.5);
					digitalWrite(LED_PIN, !digitalRead(LED_PIN)); // toggle BLUE LED
					// sprintf(txuartbuf, "rs=%d,id=%d,maj=%d,min=%d,pw=%d prx=%d\n", rssi, uuidtyp, major, minor, power, prx);
					// Serial.printf(txuartbuf);
					putBeaconDB(uuidtyp, major, minor, rssi, power,now);
					putPatBeaconDB(uuidtyp, major, minor, rssi,now);
				}
			}
		}
	}
};

static const char *wd[7] = {"Sun", "Mon", "Tue", "Wed", "Thr", "Fri", "Sat"};
/*************************************
 * NTP 同期 call back
 *************************************/
void timeavailable(struct timeval *tmp)
{
	time_t t;
	struct tm *tm;
	Serial.println("Got time adjustment from NTP!");
	t = time(NULL);
	tm = localtime(&t);
	Serial.printf("Date/Time %04d/%02d/%02d(%s) %02d:%02d:%02d\r\n",
				  tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
				  wd[tm->tm_wday],
				  tm->tm_hour, tm->tm_min, tm->tm_sec);
	gotNTP = true;
}

/********************************************
 *  BLE scan start
 ********************************************/
int scan_start()
{

	pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
	pBLEScan->setActiveScan(false); // passive, active scan uses more power, but get results faster
	return (0);
}

/*****************************************
 * BLE scan stop
 ****************************************/
int scan_stop()
{
	pBLEScan->stop();
	// delete pBLEScan;
	return (0);
}

extern int myfunc();

void interruptHandler()
{
	Serial.println("Interrupt detected!");
//	digitalWrite(PATL_PIN, HIGH);
//	memset((char *)pat_beacos, 0, sizeof(pat_beacos));
}

int is_valid_int(char *str) {
    if (strlen(str)!= 15) {
        return 0;
    }
    for (int i = 0; i < strlen(str); i++) {
        if (!isdigit(str[i])) {
            return 0;
        }
    }
    return 1;
}

/****************************************
 * Setup UART, BLE scan
 ***************************************/
void setup()
{
	// put your setup code here, to run once:
	//static uint8_t buffer[2000];
	Serial.begin(115200);
	restore_EEPROM();
//	pinMode(PATL_PIN, OUTPUT_OPEN_DRAIN); // Patlight out
//	pinMode(PATBT_PIN, INPUT);			  // Button
	digitalWrite(LED_PIN, HIGH);
	pinMode(LED_PIN, OUTPUT);
//	digitalWrite(PATL_PIN, HIGH);
//	attachInterrupt(digitalPinToInterrupt(PATBT_PIN), interruptHandler, RISING);
    //Get config from Server
#ifdef USE_LTE
	lteSetup();
	getSIMINFO(imei_str, imsi_str);
    if ( is_valid_int(imei_str) == 0 || is_valid_int(imsi_str) == 0) {
		Serial.printf("sim number err\n");
		getSIMINFO(imei_str, imsi_str);
	}
	imei = parseInt(imei_str);
	netSetup();
	wait_registration();
	delay(10);
	GetTimeLTE();
	Serial.println("============https=============");
	//    httpGetLTE(CONF_HOST, 8088 , CONFIGURL3 , buffer);
	httpsGetLTE(CONF_HOST, CONF_PORT, CONFIGURL3, sendbuffer);
	char *p = strchr((char *)sendbuffer, '{');
	if (p) { 
		get_json_data(p);
		save_EEPROM();
	}
#else
	imei = parseInt(imei_str);
    #ifdef USE_ETHER
     Ether_setup();
    #else
     wifiSetup();
    #endif
	// httpGet("http://kd119106213195.ppp-bb.dion.ne.jp:8088/update", buffer);
	Serial.println("============https=============");
	httpsGets(CONF_HOST, CONF_PORT, CONF_PAGE, sendbuffer);
	char *p = strchr((char *)sendbuffer, '{');
	if (p) { 
		get_json_data(p);
		save_EEPROM();
	}
	// NTP time setup
	sntp_set_time_sync_notification_cb(timeavailable);
	sntp_servermode_dhcp(1); // (optional)
	gotNTP = false;
	configTime(JST, 0, ntpServer1, ntpServer2);
	while (!gotNTP)
		sys_delay_ms(100);
#endif
	save_EEPROM();
	BLEDevice::init("");
	pBLEScan = BLEDevice::getScan(); // create new scan
	//scan_start();
	//scan_stop();
	Serial.printf("BLE scan Ver1.01\n");
	Serial.println("Configuring WDT...");
	esp_task_wdt_init(15, true); // 15sec enable panic so ESP32 restarts
	esp_task_wdt_add(NULL);		 // add current thread to WDT watch
	esp_task_wdt_reset();
	// Blue start TASK
	xTaskCreatePinnedToCore(threadFunction, "Task1", 1024 * 5, NULL, 3, &th, 0); // Task1実行
}

/****************************************
 * printf iBeacon
 ****************************************/
void loop()
{
	// Start BLE scan
	// BLEScanResults foundDevices = pBLEScan->start(3);
	main_loop();
}

/*****************************
 *   hex to bin 8bit
 *******************************/
static char hex2bin(char c)
{
	if (c >= '0' && c <= '9')
	{
		return c - '0';
	}
	else if (c >= 'a' && c <= 'f')
	{
		return c - 'a' + 10;
	}
	else if (c >= 'A' && c <= 'F')
	{
		return c - 'A' + 10;
	}
	else
	{
		return 0;
	}
}

/*************************************
 *  convert uuid hex to bin
 ************************************/
static void uuid2bin(char *uuidstr, char *uuidbin)
{
	for (int i = 0, j = 0; i < 36; i++)
	{
		if (*uuidstr == '-')
			uuidstr++;
		*uuidbin++ = (hex2bin(uuidstr[0]) << 4) | hex2bin(uuidstr[1]);
		uuidstr += 2;
	}
}

/**********************************************
 *     decode config server's json format
 * host
 * ivl
 * ntp
 * hb
 * hb_ivl
 * uuids[]   ud_id         ud    ->> struct uuids
 * firm_v
 * firm_p
 * tile wwan_m
 * bdb
 * bdb_rst
 * gw_rb             -> server_set.
 * pato_ivl    pato light detection interval
 * path_RSI    pato light RSSI
 *******************************************/
int get_json_data(char *jsn)
{
	char buff[60] = {0};
	char *p, *po;
	// JSONオブジェクトを作成
	DynamicJsonDocument doc(1024);
	// JSONをデコード
	deserializeJson(doc, String(jsn));
	// 配列要素を取得
	JsonVariant juuids = doc["uuids"];
	// 要素数を取得
	int uusize = juuids.size();
	if (uusize > MAX_UUIDS)
		uusize = MAX_UUIDS;
	memset(uuids, 0, sizeof(uuids));
	// 要素をループして処理
	for (int i = 0; i < uusize; i++)
	{
		char uuidstr[38] = {0};
		// 要素を取得
		JsonObject juuid = juuids[i];
		juuid["ud"].as<String>().toCharArray(uuidstr, 37);
		uuid2bin(uuidstr, (char *)uuids[i].uuid);
		uuids[i].enable = juuid["ud_id"].as<int>();
		// 結果を出力
		char data[16];
		memcpy(data, (char *)uuids[i].uuid, 16);
		Serial.printf("ud:%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X", data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7], data[8], data[9], data[10], data[11], data[12], data[13], data[14], data[15], data[16]);
		Serial.printf(" %d\n", uuids[i].enable);
	}
	doc["host"].as<String>().toCharArray(buff, 59);
	p = strstr(buff, "udp://");
	if (p)
	{
		p += 6;
		po = strchr(p, ':');
		server_set.host_port = atoi(po + 1);
		po[0] = 0;
		strcpy(server_set.host_addr, p);
	}
	server_set.ivl = doc["ivl"].as<int>();
	doc["hb"].as<String>().toCharArray(buff, 59);
	p = strstr(buff, "udp://");
	if (p)
	{
		p += 6;
		po = strchr(p, ':');
		server_set.hb_port = atoi(po + 1);
		po[0] = 0;
		strcpy(server_set.hb_addr, p);
	}
	doc["ntp"].as<String>().toCharArray(buff, 59);
	strcpy(server_set.ntpServerName, buff);
	server_set.hb_ivl = doc["hb_ivl"].as<int>();
	doc["firm_v"].as<String>().toCharArray(server_set.firm_v, 12);
	doc["firm_p"].as<String>().toCharArray(server_set.firmup_addr, 100);
	server_set.bdb = doc["bdb"].as<int>();
	server_set.bdb_rst = doc["bdb_rst"].as<int>();
	server_set.gw_rb = doc["gw_rb"].as<int>();

	server_set.pato_ivl = doc["pt_ivl"].as<int>(); // Pato light interval
	server_set.pato_RSI = doc["pt_RSI"].as<int>(); // Pato light RSSI thresh hold
	/////////////////
	Serial.printf("ntp:%s ivl:%d hb_ivl:%d  bdb:%d bdb_rst:%d  gw_rb:%d\n", server_set.ntpServerName, server_set.ivl, server_set.hb_ivl, server_set.bdb, server_set.gw_rb, server_set.gw_rb);
	Serial.printf("udp host:%s port:%d hb_host:%s\n", server_set.host_addr, server_set.host_port, server_set.hb_addr);
	Serial.printf("firmup host:%s  \n", server_set.firmup_addr);
	Serial.printf("Pato ivl:%d PaftoRSSI:%d \n", server_set.pato_ivl, server_set.pato_RSI);
	if (server_set.pato_ivl <= 0)
		server_set.pato_ivl = 30;
	if (server_set.pato_RSI >= 0)
		server_set.pato_RSI = -90;
	return (0);
}

static int bScan = false;
/**************************************
 * Just request start BLE scan
 **************************************/
void threadFunction(void *pvParameters)
{
	while (true)
	{
		// Serial.printf("Thread===\n");
		if (bScan)
			pBLEScan->start(6); // 6 sec
		else
			vTaskDelay(1000); // freeRTOS用のディレイタイム実行
		// delay(1000);
	}
}

#define START_SCAN 0
#define WAIT_SEND 1
#define SEND_UDP 2
#define CHECK_CONFIG 3
#define SET_CONFIG 4
#define GET_NTP 5

/*********************************************
  main loop

***********************************************/
int main_loop(void)
{
	int err_code;
	int i, ret;
	int buzzur = 0;
	static int syssts = START_SCAN;
	static int oldsyssts = -1;

	time_t nextSendTime;
	time_t nextHBTime;
	time_t nextGetConfigTime;
	time_t nextGetNTPTime;
	int len;
	int recev_config = 0;

	BLEScanResults foundDevices;

	nextHBTime = time(NULL) + server_set.hb_ivl;
	nextGetNTPTime = nextSendTime = time(NULL) + server_set.ivl;
	nextGetConfigTime = time(NULL) + 3600 * 24;
	esp_task_wdt_reset();
	//scan_stop();
//	enableGPS();
	while (1)
	{
		if (oldsyssts != syssts)
		{
			Serial.printf("syssts=%d\n", syssts);
			oldsyssts = syssts;
		}

		switch (syssts)
		{
		case START_SCAN: // BLE new Scan start
			//ClearBeaconDB();
			syssts = WAIT_SEND;
			scan_start();
			bScan = true;
			break;
		case WAIT_SEND:
			if (nextSendTime <= time(NULL))
				syssts = SEND_UDP;
			// else
			//	syssts = CHECK_CONFIG;
			break;
		case SEND_UDP:
			bScan = false;
			i = 0;
			int npacket;

			if (nextHBTime <= time(NULL))
			{ // Heart beat
				len = makeBCPayload(sendbuffer, -1);
#ifdef USE_LTE
				ret = sendUDPLTE(server_set.hb_addr, server_set.hb_port, sendbuffer, len);
#else
				ret = sendUDP(server_set.hb_addr, server_set.hb_port, sendbuffer, len);
#endif
				nextHBTime = time(NULL) + server_set.hb_ivl;
			}
			esp_task_wdt_reset();
			// get Beacon num
			// get number of otta Beacon
			for (i = 0; i < MAX_BEACON_COUNT; i++)
				if (beacos[i].count == 0)
					break;
			npacket = (i + 119) / 120;
			nextSendTime = time(NULL) + server_set.ivl;
			for (i = 0; i < npacket; i++)
			{
				// noInterrupts();
			    scan_stop();
				len = makeBCPayload(sendbuffer, i * 120);
			    scan_start();
				// interrupts();
				memcpy(cipherTextOutput, sendbuffer, 13);
				len = encryptUDP((char *)imsi_str, (char *)imei_str, len - 13, sendbuffer + 13, cipherTextOutput + 13);
#ifdef USE_LTE
				disableGPS();
				ret = sendUDPLTE(server_set.host_addr, server_set.host_port, cipherTextOutput, len + 13);
				enableGPS();
#else
				ret = sendUDP(server_set.host_addr, server_set.host_port, cipherTextOutput, len + 13);
#endif
				esp_task_wdt_reset();
			}
			ClearBeaconDB();
			syssts = CHECK_CONFIG;
			bScan = true;
			break;
		case CHECK_CONFIG:
			// 現在の日付と時刻を取得
			struct tm timeinfo;
			syssts = GET_NTP;
			if (getLocalTime(&timeinfo) == 0)
			{
				if (timeinfo.tm_wday == 1 && timeinfo.tm_hour == 1 && timeinfo.tm_min == 0 && nextGetConfigTime <= time(NULL))
				{
					nextGetConfigTime = time(NULL) + 3600 * 24 * 1;
					syssts = SET_CONFIG;
				}
			}
			break;
		case SET_CONFIG:

#ifdef USE_LTE
			Serial.println("============https=============");
			disableGPS();
			memset(sendbuffer,0,sizeof(sendbuffer));
			if (len = httpsGetLTE(CONF_HOST, CONF_PORT, CONFIGURL3, sendbuffer))
			{
				char *p = strchr((char *)sendbuffer, '{');
				if (p)
				{
					get_json_data(p);
					save_EEPROM();
				}
			}
			enableGPS();
#else
			memset(sendbuffer,0,sizeof(sendbuffer));
			if (len = httpsGets(CONF_HOST, CONF_PORT, CONF_PAGE, sendbuffer))
			{
				char *p = strchr((char *)sendbuffer, '{');
				if (p)
				{
					get_json_data(p);
					save_EEPROM();
				}
			}
#endif
			syssts = GET_NTP;
			break;
		case GET_NTP:
			if (nextGetNTPTime <= time(NULL))
			{
				nextGetNTPTime = time(NULL) + 3600 * 24 * 1;
#ifdef USE_LTE
				GetTimeLTE();
#else
				// NTP time setup
				sntp_set_time_sync_notification_cb(timeavailable);
				sntp_servermode_dhcp(1); // (optional)
				gotNTP = false;
				configTime(JST, 0, server_set.ntpServerName, ntpServer2);
				while (!gotNTP)
					sys_delay_ms(100);
#endif
			}
			syssts = START_SCAN;
			break;
		}
		//getLoaction();
		time_t now = time(NULL);
		buzzur = 0;
        #if 0 //Patolight
		noInterrupts();
		for (i = 0; i < MAX_BEACON_COUNT; i++)
		{
			if (server_set.pato_ivl && pat_beacos[i].lasttime)
			{
				if (now > pat_beacos[i].lasttime + server_set.pato_ivl)
				{
					interrupts();
					buzzur |= 1;
					Serial.printf("Booooo  udid:%d maj=%d minor=%d \n", pat_beacos[i].uuidtype, pat_beacos[i].major, pat_beacos[i].minor);
				}
			}
		}
		interrupts();
		digitalWrite(PATL_PIN, !buzzur);
        #endif

		// Serial.printf(".");
		esp_task_wdt_reset();
		delay(1000);
		yield();
	}
}

/**********************************************
 *  UDP encrypt
 **********************************************/
int encryptUDP(char *pimsi, char *pimei, int payloadlen, unsigned char *payloadIN, unsigned char *payloadOUT)
{
	unsigned char key[16] = {0};
	mbedtls_aes_context aes;
	int len, i, ret;
	len = strlen(pimsi);
	if (strchr(pimsi, ':') == NULL)
	{
		if (len != 15)
			return (-1); // not imsi
		for (i = 0; i < len; i++)
			if (pimsi[i] > '9' || pimsi[i] < '0')
				return (-2);
	}
	else if (len != 17)
		return (-1); // not mac address
	for (i = 0; i < (len + 1) / 2; i++)
	{
		key[i] = (pimsi[i * 2] & 0x0f) << 4 | (pimsi[i * 2 + 1] & 0x0f);
		key[15 - i] = 0x93 ^ ((pimsi[i * 2] & 0x0f) | ((pimsi[i * 2 + 1] & 0x0f) << 4));
	}
	// for (i = 0; i < 16; i++)
	//	printf("%02x ", key[i]);
	mbedtls_aes_init(&aes);
	mbedtls_aes_setkey_enc(&aes, (const unsigned char *)key, 128);
	for (i = 0; i < (payloadlen + 15) / 16; i++)
	{
		ret = mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, (const unsigned char *)payloadIN + i * 16, payloadOUT + i * 16);
		// printf("ret=%d i=%d\r\n",ret,i);
	}
	mbedtls_aes_free(&aes);
	return ((payloadlen + 15) & 0xfffffff0);
}

/**********************************************
 *  UDP decrypt
 **********************************************/
int decryptUDP(char *pimsi, char *pimei, int payloadlen, unsigned char *payloadIN, unsigned char *payloadOUT)
{
	unsigned char key[16] = {0};
	mbedtls_aes_context aes;
	int len, i, ret;
	len = strlen(pimsi);
	if (strchr(pimsi, ':') == NULL)
	{
		if (len != 15)
			return (-1); // not imsi
		for (i = 0; i < len; i++)
			if (pimsi[i] > '9' || pimsi[i] < '0')
				return (-2);
	}
	else if (len != 17)
		return (-1); // no mac address
	for (i = 0; i < (len + 1) / 2; i++)
	{
		key[i] = (pimsi[i * 2] & 0x0f) << 4 | (pimsi[i * 2 + 1] & 0x0f);
		key[15 - i] = 0x93 ^ ((pimsi[i * 2] & 0x0f) | ((pimsi[i * 2 + 1] & 0x0f) << 4));
	}
	mbedtls_aes_init(&aes);
	mbedtls_aes_setkey_dec(&aes, (const unsigned char *)key, 128);
	for (i = 0; i < (payloadlen + 15) / 16; i++)
	{
		ret = mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_DECRYPT, (const unsigned char *)payloadIN + i * 16, payloadOUT + i * 16);
		// printf("ret=%d\r\n",ret);
	}
	mbedtls_aes_free(&aes);
	return (payloadlen);
}

/*********************************************
 Setting をEEPROMに保存
**********************************************/
static void save_EEPROM()
{
#if 1
	int i;
	int bw = 0;
	uint8_t ids[2];
	uint8_t *p = (uint8_t *)&server_set;
	EEPROM.begin((sizeof(server_set) + sizeof(uuids) + 3) & 0xfffc); // EEPROM 確保
	// Save 内容と同じか？
	for (i = 0; i < sizeof(server_set); i++)
	{
		if (EEPROM.read(i) != *p++)
		{
			bw = 1;
			break;
		}
	}
	if (bw == 0)
	{
		p = (uint8_t *)&uuids;
		for (; i < sizeof(uuids) + sizeof(server_set); i++)
		{
			if (EEPROM.read(i) != *p++)
			{
				bw = 1;
				break;
			}
		}
	}
	// save
	if (bw)
	{
		server_set.id = 0xa3a3;
		p = (uint8_t *)&server_set;
		for (i = 0; i < sizeof(server_set); i++)
		{
			EEPROM.write(i, *p++); // EEPROM
		}
		p = (uint8_t *)&uuids;
		for (i = 0; i < sizeof(uuids); i++)
		{
			EEPROM.write(sizeof(server_set) + i, *p++); // EEPROM
		}
		// EEPROM.put(0,settings); //  EEPROMに書く
		Serial.printf("EEPROM save \n");
		EEPROM.commit();
	}
	EEPROM.end();
#endif
}
/*********************************************
 EEPROMから Setting
**********************************************/
static void restore_EEPROM()
{
#if 1
	int i;
	uint8_t *p = (uint8_t *)&server_set;
	uint8_t c;
	uint8_t ids[2];
	EEPROM.begin((sizeof(server_set) + sizeof(uuids) + 3) & 0xfffc); // EEPROM 確保
	EEPROM.get(0, ids[0]);											 // EEPROMより読み込み
	EEPROM.get(1, ids[1]);											 // EEPROMより読み込み
	if (ids[0] == 0xa3 && ids[1] == 0xa3)
	{
		EEPROM.get(0, server_set);			   //  yo mu
		EEPROM.get(sizeof(server_set), uuids); //  yo mu
		Serial.printf("EEPROM Restore %04x\n", server_set.id);
	}
	EEPROM.end();
#endif
}

