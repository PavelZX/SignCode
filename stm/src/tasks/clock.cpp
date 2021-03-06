#include "clock.h"
#include "rng.h"
#include <time.h>

#include "fonts/dejavu_12.h"
#include "draw.h"

extern uint64_t rtc_time;
extern matrix_type matrix;

bool tasks::ClockScreen::init() {
	name[0] = 'c';
	name[1] = 'k';
	name[2] = 's';
	name[3] = 'n';

	bg_color[0] = std::max(rng::getclr(), 50_c);
	bg_color[1] = std::max(rng::getclr(), 50_c);
	bg_color[2] = std::max(rng::getclr(), 50_c);

	return true;
}

void tasks::ClockScreen::loop() {
	struct tm timedat;
	time_t now = rtc_time / 1000;
	char buf[6];
	gmtime_r(&now, &timedat);

	snprintf(buf, 6, "%02d:%02d", timedat.tm_hour, timedat.tm_min);

	uint16_t width = draw::text_size(buf, font::dejavusans_12::info);
	draw::text(matrix.get_inactive_buffer(), buf, font::dejavusans_12::info, 60 - (width / 2) + draw::distorted_ease_wave(rtc_time, 150, 850, 8), 11, bg_color[0], bg_color[1], bg_color[2]);
}
