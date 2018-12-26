#include "time.h"
#include "serial.h"
#include "config.h"
#include "common/slots.h"

#include <TimeLib.h>
#include <NtpClientLib.h>
#include <Timezone.h>

TimeChangeRule usEDT = {"EDT", Second, Sun, Mar, 2, -240};
TimeChangeRule usEST = {"EST", First, Sun, Nov, 2, -300};
Timezone torontoTZ(usEDT, usEST);

bool active = false;

void time::init() {
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

void time::start() {
	const char * time_server = config::manager.get_value(config::TIME_ZONE_SERVER, "pool.ntp.org");
	NTP.onNTPSyncEvent([](NTPSyncEvent_t e){active=true;});
	NTP.begin(time_server);
	Serial1.print("Starting timeserver with ");
	Serial1.println(time_server);
}

void time::stop() {
	NTP.stop();
	active = false;
}

uint64_t time::get_time() {
	Serial1.print(F("now() returns"));
	Serial1.println(now());
	return static_cast<uint64_t>(torontoTZ.toLocal(now())) * 1000;
}
