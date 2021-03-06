#include "ttc.h"
#include "json.h"
#include "serial.h"
#include "stime.h"
#include "TimeLib.h"
#include "wifi.h"
#include <inttypes.h>
#include "config.h"
#include "string.h"
#include "util.h"
#include "vstr.h"
#include <time.h>

slots::TTCInfo ttc::info;
slots::TTCTime ttc::times[6];
serial::VStrSender vss_alerts;

uint64_t ttc::time_since_last_update = 0;
uint64_t      time_since_last_alert = 0;
char * ttc::alert_buffer = nullptr;

void ttc::init() {
	memset(ttc::times, 0, sizeof(ttc::times));
    ttc::info = { 0 };

	serial::interface.register_handler(&ttc::on_open);
	serial::interface.register_handler([](uint16_t data_id, uint8_t * buffer, uint8_t & length){
		if (data_id == slots::TTC_ALERTSTR) {
			vss_alerts(buffer, length);
			return true;
		}
		return false;
	});
}

void ttc::loop() {
	if ((now() - time_since_last_update > 15 || time_since_last_update == 0) && wifi::available()) {
		// update the ttc times
		auto oldflags = ttc::info.flags;
		ttc::info.flags &= (slots::TTCInfo::SUBWAY_ALERT | slots::TTCInfo::SUBWAY_OFF | slots::TTCInfo::SUBWAY_DELAYED);
		for (uint8_t slot = 0; slot < 3; ++slot) {
			const char * stopid = config::manager.get_value((config::Entry)(config::STOPID1 + slot));
			if (stopid != nullptr) {
				const char * dtag = config::manager.get_value((config::Entry)(config::DTAG1 + slot));
				const char * name = config::manager.get_value((config::Entry)(config::SNAME1 + slot));
				serial::interface.update_data(slots::TTC_NAME_1 + slot, (const uint8_t *)name, strlen(name));
				ttc::info.nameLen[slot] = strlen(name);

				if (!do_update(stopid, dtag, slot)) {
					if (oldflags & (slots::TTCInfo::EXIST_0 << slot)) ttc::info.flags |= slots::TTCInfo::EXIST_0 << slot;
				}
			}
		}

		serial::interface.update_data(slots::TTC_INFO, (const uint8_t *)&info, sizeof(info));
		time_since_last_update = now();
	}
}

void ttc::on_open(uint16_t data_id) {
	Log.println(F("onopen: "));
	Log.println(data_id);
	switch (data_id) {
		case slots::TTC_INFO:
			serial::interface.update_data(slots::TTC_INFO, (const uint8_t *)&info, sizeof(info));
			break;
		case slots::TTC_TIME_1:
		case slots::TTC_TIME_2:
		case slots::TTC_TIME_3:
			serial::interface.update_data(data_id, (const uint8_t *)(&times[data_id - slots::TTC_TIME_1]), sizeof(slots::TTCTime));
			break;
		case slots::TTC_TIME_1B:
		case slots::TTC_TIME_2B:
		case slots::TTC_TIME_3B:
			serial::interface.update_data(data_id, (const uint8_t *)(&times[3 + data_id - slots::TTC_TIME_1B]), sizeof(slots::TTCTime));
			break;
		case slots::TTC_NAME_1:
		case slots::TTC_NAME_2:
		case slots::TTC_NAME_3:
			{
				const char * name = config::manager.get_value((config::Entry)(config::SNAME1 + (data_id - slots::TTC_NAME_1)));
				if (name == nullptr) return;
				serial::interface.update_data(data_id, (const uint8_t *)name, strlen(name));
				break;
			}
	}
}

bool ttc::do_update(const char * stop, const char * dtag, uint8_t slot) {
	char url[80];
	snprintf_P(url, 80, PSTR("/service/publicJSONFeed?command=predictions&a=ttc&stopId=%s"), stop);

	Log.println(url);
	int16_t status_code;
	auto cb = util::download_with_callback("webservices.nextbus.com", url, status_code);

	if (status_code < 200 || status_code > 299) {
		util::stop_download();
		return false;
	}

	// message is here now read it
	
	struct State {
		uint8_t e1;
		uint8_t e2;

		uint64_t epoch = 0;
		bool layover = false;
		bool tag = false;
	} state;

	char * dirtag = strdup(dtag);
	
	json::JSONParser parser([&](json::PathNode ** stack, uint8_t stack_ptr, const json::Value& v){
		if (stack_ptr < 2) return;

		json::PathNode &top = *stack[stack_ptr-1];
		json::PathNode &parent = *stack[stack_ptr-2];

		if ((parent.is_array() || parent.is_obj()) && strcmp_P(parent.name, PSTR("prediction")) == 0) {
			// use the first two only

			state.e1 = stack[1]->index;
			state.e2 = parent.index;
			
			if (strcmp_P(top.name, PSTR("affectedByLayover")) == 0) {
				// is it's value true?
				if (v.type == json::Value::STR && strcmp_P(v.str_val, PSTR("true")) == 0) {
					// mark as delayed
					state.layover = true;
				}
				else {
					state.layover = false;
				}
			}
			if (strcmp_P(top.name, PSTR("dirTag")) == 0 && v.type == json::Value::STR)
			{
				strcpy(dirtag, dtag);
				char *test_str = strtok(dirtag, ",");
				while (test_str != NULL) {
					Log.println(test_str);
					Log.println(v.str_val);
					Log.println(parent.is_array() ? parent.index : -1);
					if (strcmp(test_str, v.str_val) == 0) {
						state.tag = true; break;
					}
					else {
						test_str = strtok(NULL, ",");
					}
				}
			}
			if (strcmp_P(top.name, PSTR("epochTime")) == 0 && v.type == json::Value::STR) {
				state.epoch = signtime::millis_to_local(atoll(v.str_val));
			}
		}
		else if (top.is_array() && strcmp_P(top.name, PSTR("prediction")) == 0 && v.type == json::Value::OBJ) {
			if (state.tag && state.e2 < 4) {
				ttc::info.flags |= (slots::TTCInfo::EXIST_0 << slot);
				if (state.epoch < ttc::times[slot].tA || ttc::times[slot].tA == 0) {
					ttc::times[slot+3].tB = ttc::times[slot+3].tA;
					ttc::times[slot+3].tA = ttc::times[slot].tB;
					ttc::times[slot].tB = ttc::times[slot].tA;
					ttc::times[slot].tA = state.epoch;
					if (state.layover) {
						ttc::info.flags |= (slots::TTCInfo::DELAY_0 << slot);
					}
				}
				else if (state.epoch < ttc::times[slot].tB || ttc::times[slot].tB == 0) {
					ttc::times[slot+3].tB = ttc::times[slot+3].tA;
					ttc::times[slot+3].tA = ttc::times[slot].tB;
					ttc::times[slot].tB = state.epoch;
					if (state.layover) {
						ttc::info.flags |= (slots::TTCInfo::DELAY_0 << slot);
					}
				}
				else if (state.epoch < ttc::times[slot+3].tA || ttc::times[slot+3].tA == 0) {
					ttc::times[slot+3].tB = ttc::times[slot+3].tA;
					ttc::times[slot+3].tA = state.epoch;
					if (state.layover) {
						ttc::info.flags |= (slots::TTCInfo::DELAY_0 << slot);
					}
				}
				else if (state.epoch < ttc::times[slot+3].tB || ttc::times[slot+3].tB == 0) {
					ttc::times[slot+3].tB = state.epoch;
					if (state.layover) {
						ttc::info.flags |= (slots::TTCInfo::DELAY_0 << slot);
					}
				}

				Log.print(F("Adding ttc entry in slot "));
				Log.print(slot);
			}
			state.tag = false;
			state.layover = false;
			state.epoch = 0;
		}
	});

	auto bkp1 = ttc::times[slot], bkp2 = ttc::times[slot + 3];

	memset(&ttc::times[slot], 0, sizeof(ttc::times[0]));
	memset(&ttc::times[slot+3], 0, sizeof(ttc::times[0]));

	bool ok = true;

	if (!parser.parse(std::move(cb))) {
		Log.println(F("JSON fucked up."));

		ttc::times[slot] = bkp1;
		ttc::times[slot + 3] = bkp2;

		ok = false;
	} // parse while calling our function.

	on_open(slots::TTC_TIME_1 + slot);
	on_open(slots::TTC_TIME_1B + slot);

	util::stop_download();
	free(dirtag);

	return ok;
}
