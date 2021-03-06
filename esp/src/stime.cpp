#include "stime.h"
#include "serial.h"
#include "config.h"
#include "common/slots.h"
#include "util.h"

#include <TimeLib.h>
#include <NtpClientLib.h>
#include <Timezone.h>

TimeChangeRule usEDT = {"EDT", Second, Sun, Mar, 2, -240};
TimeChangeRule usEST = {"EST", First, Sun, Nov, 2, -300};
Timezone torontoTZ(usEDT, usEST);

bool active = false;

void signtime::init() {
	serial::interface.register_handler([](uint16_t data_id, uint8_t * buffer, uint8_t & length) {
			if (data_id == slots::TIME_OF_DAY && active) {
				uint64_t current_time = get_time();
				if (current_time < 10000000) {
					return false;
				}
				if (timeStatus() != timeSet) {
					return false;
				}
				memcpy(buffer, &current_time, 8);
				length = 8;
				return true;
			}
			return false;
	});
}

void signtime::start() {
	const char * time_server = config::manager.get_value(config::TIME_ZONE_SERVER, "pool.ntp.org");
	NTP.onNTPSyncEvent([](NTPSyncEvent_t e){active=true;});
	NTP.begin(time_server);
	Log.print("Starting timeserver with ");
	Log.println(time_server);
}

void signtime::stop() {
	NTP.stop();
	active = false;
}

uint64_t signtime::get_time() {
	return static_cast<uint64_t>(torontoTZ.toLocal(now())) * 1000;
}

uint64_t signtime::to_local(uint64_t utc){
	return (torontoTZ.toLocal(utc));
}
uint64_t signtime::millis_to_local(uint64_t js_utc){
	return (js_utc % 1000) + to_local(js_utc / 1000) * 1000;
}
