#include <Adafruit_NeoPixel.h>
#include <ESP8266HTTPClient.h>

#include <ESP8266WiFi.h>
#include <TwiFi.h>

typedef struct RGB {
    float r;
    float g;
    float b;
};

#include "Configuration.h"
#include "ConfigurationWiFi.h"
#include "ConfigurationLuna.h"


#define PIN_PIXEL  13
#define PIN_BUTTON 12

#define RESULT_SUCCESS   0
#define RESULT_ERROR     1
#define RESULT_NO_LYRICS 2

#define URL_RESULT_DONE 0
#define URL_RESULT_FAIL 1

#define STATUS_NO_LYRICS 0
#define STATUS_LYRICS    1
#define STATUS_ERROR     2


/* LEDs */
Adafruit_NeoPixel strip = Adafruit_NeoPixel(1, PIN_PIXEL, NEO_GRB + NEO_KHZ800);

struct LEDMain {
	RGB led {ledIdleNoLyrics.r, ledIdleNoLyrics.g, ledIdleNoLyrics.b};
} ledMain;

/* Misc. Values */
bool buttonPressed = false;
int holdCounter = 0;
int statusCounter = 0;
int failCounter = 0;
int status = STATUS_NO_LYRICS;
String npOld = "";

void setupPins();
void setupLED();

void processButtons();
void processStatus();
void processTicks();

void ledFlicker();
void ledSet();
int openL();
int openURL(String url);
String splitString(String data, char separator, int index);
void checkNewTrack(String npNew);

void connectAttempt(int idEntry, int attempt);
void connectSuccess(int idEntry);
void connectFail(int idEntry);


void setupPins() {
    pinMode(PIN_BUTTON, INPUT_PULLUP);
}

void setupLED() {
    strip.begin();
    ledSet();
}   

void processButtons() {
    if (digitalRead(PIN_BUTTON) == LOW && !buttonPressed) {
        buttonPressed = true;
        holdCounter = 0;
        statusCounter = 0;

        float rTar = ledIdleNoLyrics.r;
        float gTar = ledIdleNoLyrics.g;
        float bTar = ledIdleNoLyrics.b;
        if (status == STATUS_LYRICS) {
            rTar = ledIdleLyrics.r;
            gTar = ledIdleLyrics.g;
            bTar = ledIdleLyrics.b;
        }
        else if (status == STATUS_ERROR) {
            rTar = ledIdleError.r;
            gTar = ledIdleError.g;
            bTar = ledIdleError.b;
        }
        ledMain.led.r = rTar * LED_PRESSED_MULTI;
        ledMain.led.g = gTar * LED_PRESSED_MULTI;
        ledMain.led.b = bTar * LED_PRESSED_MULTI;
        ledSet();

        int result = openL();
        switch (result) {
            case RESULT_SUCCESS:
                ledMain.led.r = ledNotifSuccess.r;
                ledMain.led.g = ledNotifSuccess.g;
                ledMain.led.b = ledNotifSuccess.b;
                ledSet();
                break;
            case RESULT_ERROR:
                ledFlicker();
                break;
            case RESULT_NO_LYRICS:
                ledMain.led.r = ledNotifNoLyrics.r;
                ledMain.led.g = ledNotifNoLyrics.g;
                ledMain.led.b = ledNotifNoLyrics.b;
                ledSet();
                break;
        }
    }
    else if (digitalRead(PIN_BUTTON) == HIGH && buttonPressed) {
        buttonPressed = false;
    }
}

void processStatus() {
    if (statusCounter >= LYRICS_INTERVAL / RUNTIME_STEP) {
        statusCounter = 0;
        openLQ();
    }
    else {
        statusCounter++;
    }
}

void processTicks() {
    if (holdCounter >= LED_NOTIF_HOLD / RUNTIME_STEP) {
        float rTar = ledIdleNoLyrics.r;
        float gTar = ledIdleNoLyrics.g;
        float bTar = ledIdleNoLyrics.b;
        if (status == STATUS_LYRICS) {
            rTar = ledIdleLyrics.r;
            gTar = ledIdleLyrics.g;
            bTar = ledIdleLyrics.b;
        }
        else if (status == STATUS_ERROR) {
            rTar = ledIdleError.r;
            gTar = ledIdleError.g;
            bTar = ledIdleError.b;
        }

        float div = LED_NOTIF_FADE / RUNTIME_STEP;
        float rOffs = (rTar - ledMain.led.r) / div;
        float gOffs = (gTar - ledMain.led.g) / div;
        float bOffs = (bTar - ledMain.led.b) / div;

        ledMain.led.r += rOffs;
        ledMain.led.g += gOffs;
        ledMain.led.b += bOffs;
    }
    else {
        holdCounter++;
    }

    ledSet();
}

void ledFlicker() {
    for (int i = 0; i < LED_ERROR_FLICKER_CNT; i++) {
        ledMain.led.r = 0;
        ledMain.led.g = 0;
        ledMain.led.b = 0;
        ledSet();
        delay(LED_ERROR_FLICKER_INT);
        ledMain.led.r = ledNotifError.r;
        ledMain.led.g = ledNotifError.g;
        ledMain.led.b = ledNotifError.b;
        ledSet();
        delay(LED_ERROR_FLICKER_INT);
    }
}

void ledSet() {
    strip.setPixelColor(0, strip.Color(ledMain.led.r, ledMain.led.g, ledMain.led.b));
    strip.show();
}

int openL() {
    if (LUNA_DEBUG)
        Serial.println("Forcing lyrics!");

    HTTPClient http;

    http.begin(LUNA_IP, LUNA_PORT, String(LUNA_URL_L) + "&key=" + String(LUNA_KEY));

    int httpCode = http.GET();
    if (httpCode != 200)
        return RESULT_ERROR;

    String body = http.getString();
    http.end();

    if (body == "success")
        return RESULT_SUCCESS;
    else if (body == "no_lyrics")
        return RESULT_NO_LYRICS;
    else
        return RESULT_ERROR;
}

void openLQ() {
    if (LUNA_DEBUG)
        Serial.println("Fetching lyric info.");

    HTTPClient http;

    http.begin(LUNA_IP, LUNA_PORT, String(LUNA_URL_LQ) + "&key=" + String(LUNA_KEY));

    int httpCode = http.GET();
    if (httpCode != 200) {
        status = STATUS_ERROR;
        return;
    }

    String body = http.getString();
    http.end();

    String hasLyrics = splitString(body, '|', 0);
    String npNew = splitString(body, '|', 1);

    if (hasLyrics == "true") {
        failCounter = 0;
        checkNewTrack(npNew);
        status = STATUS_LYRICS;
    }
    else if (hasLyrics == "false") {
        failCounter = 0;
        checkNewTrack(npNew);
        status = STATUS_NO_LYRICS;
    }
    else {
        if (failCounter >= FAIL_COUNT)
            status = STATUS_ERROR;
        else
            failCounter++;
    }
}

int openURL(String url) {
    if (LUNA_DEBUG)
        Serial.println("Opening URL: " + String(LUNA_IP) + url);
        
    WiFiClient client;
    if (!client.connect(LUNA_IP, LUNA_PORT)) {  
        if (LUNA_DEBUG)
            Serial.println("Error connecting!");
        return URL_RESULT_FAIL;
    }

    client.print("GET " + url + " HTTP/1.1\r\n" +
                 "Host: " + LUNA_IP + "\r\n" + 
                 "Connection: close\r\n\r\n");
    client.stop();
    
    if (LUNA_DEBUG)
        Serial.println("Connection success.");

    return URL_RESULT_DONE;
}

String splitString(String data, char separator, int index) {
    int found = 0;
    int strIndex[] = {0, -1};
    int maxIndex = data.length() - 1;

    for(int i = 0; i <= maxIndex && found <= index; i++) {
        if(data.charAt(i) == separator || i == maxIndex) {
            found++;
            strIndex[0] = strIndex[1] + 1;
            strIndex[1] = (i == maxIndex) ? i + 1 : i;
        }
    }

    return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}

void checkNewTrack(String npNew) {
    if (npOld != npNew) {
        npOld = npNew;
        ledMain.led.r = ledNotifNewTrack.r;
        ledMain.led.g = ledNotifNewTrack.g;
        ledMain.led.b = ledNotifNewTrack.b;
        ledSet();
    }
}

void connectAttempt(int idEntry, int attempt) {
    if (attempt % 2)
        strip.setPixelColor(0, strip.Color(ledNotifWiFiSearch.r, ledNotifWiFiSearch.g, ledNotifWiFiSearch.b));
    else 
        strip.setPixelColor(0, strip.Color(0, 0, 0));
    strip.show();
}

void connectSuccess(int idEntry) {
    strip.setPixelColor(0, strip.Color(0, 0, 0));
    strip.show();
}

void connectFail(int idEntry) {
    strip.setPixelColor(0, strip.Color(ledNotifWiFiFail.r, ledNotifWiFiFail.g, ledNotifWiFiFail.b));
    strip.show();
    delay(500);
    strip.setPixelColor(0, strip.Color(0, 0, 0));
    strip.show();
}

void setup() {
    Serial.begin(115200);

    setupPins();
    setupLED();

    twifiInit(
        wifis,
        WIFI_COUNT,
        WIFI_HOST,
        WIFI_TIMEOUT,
        &connectAttempt,
        &connectSuccess,
        &connectFail,
        WIFI_DEBUG
        );
	twifiConnect(true);

	if (!LED_COLOR_DEBUG) {
        ledSet();
		openURL(String(LUNA_URL_BOOT) + "&key=" + String(LUNA_KEY) + "&device=" + String(WIFI_HOST));
	}
}

void loop() {	
	if (!LED_COLOR_DEBUG) {
        processButtons();
        processStatus();
		processTicks();

        if (!twifiIsConnected())
            twifiConnect(true);
	}
	else {
		strip.setPixelColor(0, strip.Color(ledDebug.r, ledDebug.g, ledDebug.b)); 
		strip.show();
	} 

	delay(RUNTIME_STEP);
}

