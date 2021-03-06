#include <Arduino.h>
#include <ESP8266WiFi.h>
#include "serial.h"
#include "config.h"
#include "wifi.h"
#include "stime.h"
#include "ttc.h"
#include "weather.h"
#include "sccfg.h"
#include "webui.h"
#include "upd.h"
#include "calfix.h"
#include "modelserve.h"
#include "SdFat.h"
#include "util.h"

SdFatSoftSpi<D6, D2, D5> sd;

void setup() {
	Serial.begin(115200, SERIAL_8E1);
	Serial1.begin(115200, SERIAL_8N1);
	Serial1.setDebugOutput(true);

	Serial.swap();

	Serial1.println(F("\n==== Starting Sign Backend v2. ===="));
	Serial1.println(F("You are looking at the debug output."));

	wifi::prepare();

	// check for updates
    Serial1.println("checking upd");
	auto reason = upd::needed();
	if (reason == upd::WEB_UI) {
		upd::update_website();
		ESP.restart();
	}
	else if (reason == upd::FULL_SYSTEM) {
		upd::update_system();
		Serial1.println(F("inv"));
	}

	srand(now());

	config::manager.load_from_sd();
	wifi::init();
	signtime::init();
	ttc::init();
	weather::init();
	sccfg::init();
	calfix::init();
	modelserve::init();
	webui::init();
}

bool i = false;

void loop() {
	if (!i) {
		i = serial::interface.ensure_handshake();
		return;
	}
	serial::interface.loop();
	ttc::loop();
	wifi::loop();
	weather::loop();
	sccfg::loop();
	calfix::loop();
	modelserve::loop();
	webui::loop();
	delay(1);
}
