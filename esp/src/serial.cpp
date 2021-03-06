#include "serial.h"
#include "HardwareSerial.h"
#include "Esp.h"
#include <TimeLib.h>
#include "util.h"
#include "debug.h"

serial::SerialInterface serial::interface;

uint16_t serial::search_for(uint16_t val, uint16_t array[256]) {
	for (uint8_t offset = 0; offset < 128; ++offset) {
		if (array[offset] == val) return offset;
		if (array[256-offset-1] == val) return 256-offset-1;
	}

	Log.printf_P(PSTR("Failed search %d\n"), val);

	return ~0;
}

bool serial::SerialInterface::ensure_handshake() {
	// wait for the incoming HANDSHAKE_INIT command
	uint8_t buf[3];
	if (Serial.available() < 3) {return false;}
	Serial.readBytes(buf, 3);
	if (buf[0] != 0xa5 && buf[1] == 0xa5) {
		Serial.read();
		return false;
	}

	if (!init_state) {
		if (buf[0] != 0xa5 || buf[1] != 0x00 || buf[2] != serial::HANDSHAKE_INIT) {
			return false;
		}
		buf[0] = 0xa6;
		buf[2] = serial::HANDSHAKE_RESP;

		Serial.write(buf, 3);
		init_state = true;
		return false;
	}
	else {
		if (buf[0] != 0xa5 || buf[1] != 0x00 || buf[2] != serial::HANDSHAKE_OK) {
			init_state = false;
			return false;
		}

		Log.println(F("Connected to STM32"));
		return true;
	}
}

uint32_t last_pong_at = 0;

void serial::SerialInterface::loop() {
	// Check if there is incoming data for a header.
	if (waiting_size != 0) {
		// we are waiting for incoming data
		if (Serial.available() >= waiting_size) {
			uint8_t buf[waiting_size];
			Serial.readBytes(buf, waiting_size);
			handle_command(static_cast<serial::Command>(pending_command), waiting_size, buf);
			waiting_size = 0;
		}
	}
	else if (Serial.available() >= 3) {
		uint8_t buf[3];
		Serial.readBytes(buf, 3);
		if (buf[0] == 0xa6) {
			Log.println(F("STM32 is having an identity crisis, it thinks it's an ESP8266"));
			Serial.read(); // borks everything a bit more, but it'll eventually grab enough bytes to fix things.
		}
		else if (buf[0] != 0xa5) {
			Log.println(F("STM32 is drunk; sent invalid header."));
			Serial.read();
			Serial1.printf("%02x %02x %02x\n", buf[0], buf[1], buf[2]);
		}
		else if (buf[1] != 0x00) {
			pending_command = buf[2];
			waiting_size = buf[1];
		}
		else {
			handle_command(static_cast<serial::Command>(buf[2]), 0, nullptr);
		}
	}

	if (now() > 1000 && now() - last_pong_at > 5) {
		last_pong_at = now();
		uint8_t buf_reply[3] = {
			0xa6,
			0x00,
			PONG
		};

		Serial.write(buf_reply, 3);
		Log.println(F("PONGov"));
	}
}

void serial::SerialInterface::reset() {
	uint8_t buf_reply[3] = {
		0xa6,
		0x00,
		RESET
	};

	Serial.write(buf_reply, 3);
	ESP.restart();
}

void serial::SerialInterface::handle_command(serial::Command cmd, uint8_t size, uint8_t *buf) {
	switch (cmd) {
		case HANDSHAKE_INIT:
		case HANDSHAKE_RESP:
		case HANDSHAKE_OK:
			{
				Log.println(F("Encountered handshake commands in main loop."));
				
				// Reset target, then reset us.
				reset();
			}
			break;
		case OPEN_CONN:
			{
				// open conn
				if (size < 4) goto size_error;

				uint16_t slot_type = *(uint16_t *)(buf + 2);
				if (buf[0] == 0x00) {
					uint16_t pos = search_for(slot_type, slots_continuous);
					if (pos != 0xFFFF) {
						Log.println(F("invalid value in slots_continuous"));
						slots_continuous[pos] = 0x00;
					}
					slots_continuous[buf[1]] = slot_type;
				}
				else {
					// else cont.
					slots_polled[buf[1]] = slot_type;
				}

				uint8_t buf_reply[4] = {
					0xa6,
					0x01,
					ACK_OPEN_CONN,
					buf[1]
				};

				Serial.write(buf_reply, 4);

				if (buf[0] == 0x00) {
					update_open_handlers(buf[1]);
				}
			}
			break;
		case CLOSE_CONN:
			{
				// close the connection
				if (size < 1) goto size_error;

				slots_continuous[buf[0]] = 0x00;
				slots_polled[buf[0]] = 0x00;

				uint8_t buf_reply[4] = {
					0xa6,
					0x01,
					ACK_CLOSE_CONN,
					buf[0]
				};

				Serial.write(buf_reply, 4);
				Serial.flush();
			}
			break;
		case NEW_DATA:
			{
				if (size < 1) goto size_error;

				update_polled_data(buf[0]);
			}
			break;
		case RESET:
			{
				Log.println(F("[wall] The system is going down for RESET now!"));
				ESP.restart();
			}
			break;
		case PING:
			{
				uint8_t buf_reply[3] = {
					0xa6,
					0x00,
					PONG
				};

				Serial.write(buf_reply, 3);
				Log.println(F("PONG"));
				last_pong_at = now();
			}
			break;
		case CONSOLE_MSG:
			{
				switch (buf[0]) {
					case 0x01:
						Log.println(F("invalid command for msg"));
						break;
					case 0x02:
						debug::send_msg((char *)buf + 1, size - 1);
						break;
					case 0x10:
						Log.print(F("(stm): "));
						Log.write(buf + 1, size - 1);
						break;
				}
			}
			break;
		default:
			{
				Log.print(F("Got an invalid command or command that should only come from us.\n"));
			}
	}

	return;

size_error:
	Log.printf("Size error on cmd %02x", cmd);
	return;
}


void serial::SerialInterface::send_data_to(uint8_t slot_id, const uint8_t * buffer, uint8_t length) {
	uint8_t buf[length + 4];
	buf[0] = 0xa6;
	buf[1] = length + 1;
	buf[2] = serial::SLOT_DATA;
	buf[3] = slot_id;
	memcpy(buf + 4, buffer, length);
	
	Serial.write(buf, length + 4);
}

void serial::SerialInterface::update_polled_data(uint8_t slot_id) {
	uint16_t data_id = slots_polled[slot_id];
	uint8_t buf[16];
	uint8_t length;

	for (uint8_t i = 0; i < number_of_handlers; ++i) {
		if (handlers[i](data_id, buf, length)) goto ok;
	}

	return;
ok:
	send_data_to(slot_id, buf, length);
}

void serial::SerialInterface::register_handler(const QueryHandler handler) {
	if (number_of_handlers == 8) {
		Log.println(F("Too many query handlers...\n"));
		return;
	}
	handlers[number_of_handlers++] = handler;
}

void serial::SerialInterface::register_handler(const OpenHandler handler) {
	if (number_of_o_handlers == 8) {
		Log.println(F("Too many query handlers...\n"));
		return;
	}
	o_handlers[number_of_o_handlers++] = handler;
}

void serial::SerialInterface::update_data(uint16_t data_id, const uint8_t * buffer, uint8_t length) {
	uint16_t pos = search_for(data_id, this->slots_continuous);
	if (pos != (uint16_t)(~0)) {
		send_data_to(pos, buffer, length);
	}
	else {

	}
}

void serial::SerialInterface::update_open_handlers(uint8_t slot_id) {
	uint16_t data_id = this->slots_continuous[slot_id];
	for (uint8_t i = 0; i < number_of_o_handlers; ++i) {
		Log.printf_P(PSTR("calling open handler at %p, \n"), o_handlers[i]);
		o_handlers[i](data_id);
	}
}

void serial::SerialInterface::send_console_data(const uint8_t * buf, size_t length) {
	if (length > 253) return;

	uint8_t obuf[4] = {
		0xa6,
		static_cast<uint8_t>(length + 1),
		0x70,
		0x01
	};

	Serial.write(obuf, 4);
	Serial.write(buf, length);
}

void serial::SerialInterface::register_debug_commands() {
	debug::add_command("slotstats", slotstats);
}

void serial::slotstats(const char *args, char *&buffer, const char *end) {
	buffer += sprintf_P(buffer, PSTR("---- continuous ----\n"));
	for (int i = 0; i < 256; ++i) {
		if (serial::interface.slots_continuous[i]) {
			buffer += sprintf_P(buffer, PSTR("%02x ---> %04x\n"), i, serial::interface.slots_continuous[i]);
			if (buffer + 10 > end) return;
		}
	}
	buffer += sprintf_P(buffer, PSTR("---- polled ----\n"));
	for (int i = 0; i < 256; ++i) {
		if (serial::interface.slots_polled[i]) {
			buffer += sprintf_P(buffer, PSTR("%02x ---> %04x\n"), i, serial::interface.slots_polled[i]);
			if (buffer + 10 > end) return;
		}
	}
}
