#include "wifi.h"

#include "stime.h"
#include "common/slots.h"
#include "config.h"
#include "serial.h"
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiServer.h>

WiFiEventHandler got_ip, discon;
WiFiServer ctrl_server(34476);
WiFiClient ctrl_client;

bool client_ok = false;
bool wifi_available = false;

void wifi::prepare() {
	Serial1.println("Discon.");
}

void wifi::init() {
	// setup wifi events

	wifi_available = true;
	signtime::start();

	// start up wifi with parameters loaded
	WiFi.persistent(false);
	WiFi.mode(WIFI_STA);
	WiFi.hostname("msign");
	WiFi.begin(config::manager.get_value(config::SSID), 
			   config::manager.get_value(config::PSK));

	ctrl_server.begin();
}

bool wifi::available() {
	return wifi_available;
}

void dump_config() {
	Serial1.println(F("ctrl: dump config"));
	
	for (uint8_t e = 0; e < config::ENTRY_COUNT; ++e) {
		const char * value;
		if ((value = config::manager.get_value((config::Entry)e)) != nullptr) {
			Serial1.print(config::entry_names[e]);
			ctrl_client.print(config::entry_names[e]);
			Serial1.print('=');
			ctrl_client.print('=');
			Serial1.print(value);
			ctrl_client.print(value);
			Serial1.println();
			ctrl_client.print('\n');
		}
	}

	ctrl_client.print('\x00');
}

void update_config() {
	Serial1.println(F("ctrl: update config start"));

	uint32_t payload_size;
	
	while (ctrl_client.available() < 4) {
		delay(2);
	}

	ctrl_client.readBytes((uint8_t *)&payload_size, 4);
	
	char * buf = (char *)malloc(payload_size);
	if (buf == nullptr) {
		ctrl_client.write(0x10);
		return;
	}

	Serial1.printf("ctrl: downloading conf %d\n", payload_size);

	char * pos = buf;
	uint32_t a = 0;

	while (a < payload_size) {
		if (ctrl_client.available()) {
			*pos++ = ctrl_client.read();
			Serial1.write(*(pos - 1));
			++a;
		}
		else {
			delay(5);
		}
	}

	if (!config::manager.use_new_config(buf, payload_size)) {
		ctrl_client.write(0x11);
		return;
	}

	Serial1.println(F("ctrl: update complete"));

	ctrl_client.write((uint8_t)0);

	delay(100);

	serial::interface.reset();
}

void wifi::loop() {
	if (client_ok) {
		// Handle client commands.
		if (!ctrl_client.connected()) {
			ctrl_client.stop();
			client_ok = false;
		}
		else if (ctrl_client.available()) {
			int command = ctrl_client.read();
			switch (command) {
				case 0x10:
					Serial1.println(F("ctrl: reset"));
					serial::interface.reset();
					break;
				case 0x20:
					dump_config();
					break;
				case 0x30:
					update_config();
					break;
				case 0x40:
					{
						Serial1.println(F("Starting update!"));
					}
				default:
					break;
			}
		}
	}
	else if (!ctrl_client) {
		ctrl_client = ctrl_server.available();
	}
	else {
		if (!ctrl_client.connected()) ctrl_client.stop();
		else if (ctrl_client.available() >= 2) {
			uint8_t buf[2];
			ctrl_client.read(buf, 2);
			if (buf[0] == 'M' && buf[1] == 'n') {
				client_ok = true;
				Serial1.println(F("ctrl: connected"));
			}
			else {
				ctrl_client.stop();
				client_ok = false;
			}
		}
	}
}
