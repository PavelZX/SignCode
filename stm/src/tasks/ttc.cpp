#include "ttc.h"
#include "timekeeper.h"
#include "fonts/tahoma_9.h"
#include "fonts/lcdpixel_6.h"
#include "srv.h"
#include "common/slots.h"
#include "rng.h"
#include "draw.h"

namespace bitmap {
	const uint8_t bus[] = { // really shitty bus
		0b11111111, 0b11111100,
		0b10100010, 0b00100100,
		0b10100010, 0b00100100,
		0b10111111, 0b11111100,
		0b10111111, 0b11111100,
		0b11101111, 0b10110000,
		0b00010000, 0b01000000
	}; // stride = 2, width = 14, height = 7

	const uint8_t subway[] = {
		0b11111111,0b11111111,0b00000000,
		0b10000001,0b11110000,0b10000000,
		0b10111101,0b01010110,0b10000000,
		0b10111101,0b01010110,0b01000000,
		0b10000001,0b01010000,0b01000000,
		0b11111111,0b11111111,0b11000000,
		0b11010110,0b00001101,0b01000000,
		0b00101000,0b00000010,0b10000000
	}; // w=18, h=8, stride=3
}

extern uint64_t rtc_time;
extern matrix_type matrix;
extern tasks::Timekeeper timekeeper;
extern srv::Servicer servicer;

void tasks::TTCScreen::loop() {
	// Alright, now we have to work out what to show
	const slots::TTCInfo& info = servicer.slot<slots::TTCInfo>(s_info);
	if (servicer.slot_dirty(s_info, true)) ready = true;

	int16_t y = 10;
	//if (((~info.flags) & (slots::TTCInfo::SUBWAY_ALERT | slots::TTCInfo::EXIST_0 | slots::TTCInfo::EXIST_1 | slots::TTCInfo::EXIST_2)) == 0) {
	//	y = 10 - draw::distorted_ease_wave(rtc_time, 1000, 4500, 18);
	//}

	uint8_t name[17] = {0};

	if (ready) {
		// Check first slot
		for (uint8_t slot = 0; slot < 3; ++slot) {
			if (info.nameLen[slot] > 16) {
				ready = false;
				return;
			}
			if (info.flags & (slots::TTCInfo::EXIST_0 << slot)) {
				memset(name, 0, 17);
				memcpy(name, servicer.slot(s_n[slot]), info.nameLen[slot]);

				uint64_t times[4] = {
					servicer.slot<slots::TTCTime>(s_t[slot]).tA, servicer.slot<slots::TTCTime>(s_t[slot]).tB,
					servicer.slot<slots::TTCTime>(s_tb[slot]).tA, servicer.slot<slots::TTCTime>(s_tb[slot]).tB
				};

				if (draw_slot(y, name, times, 
						info.flags & (slots::TTCInfo::ALERT_0 << slot),
						info.flags & (slots::TTCInfo::DELAY_0 << slot)
				)) {
					y += 18;
				}
			}
		}
	}

	draw::rect(matrix.get_inactive_buffer(), 0, 0, 128, 10, 0, 0, 0);
	if (!(info.flags & slots::TTCInfo::SUBWAY_ALERT)) {
		draw_bus();
	}
	else {
		s_alert.update();
		draw_alertstr();
	}
	draw::rect(matrix.get_inactive_buffer(), 0, 9, 128, 10, 50_c, 50_c, 50_c);

}

void tasks::TTCScreen::draw_alertstr() {
	if (!s_alert.data) return;
	// Calculate alertstr size
	
	const slots::TTCInfo& info = servicer.slot<slots::TTCInfo>(s_info);

	int16_t alertstr_size = 40 + draw::text_size(s_alert.data, font::tahoma_9::info);
	int16_t pos = 64 - (alertstr_size / 2);
	if (alertstr_size >= 120) {
		pos = draw::scroll(rtc_time / 9, alertstr_size);
	}

	uint16_t r = 4095, g = 4095, b = 4095;
	bool f = false;
	if (info.flags & slots::TTCInfo::SUBWAY_DELAYED) {
		f = true;
		g = 127_c;
		b = 0;
	}
	if (info.flags & slots::TTCInfo::SUBWAY_OFF) {
		if (!f) {
			g = 10;
			b = 10;
		}
		else {
			g -= draw::distorted_ease_wave(rtc_time, 800, 1800, 117);
			b += draw::distorted_ease_wave(rtc_time, 800, 1800, 10);
		}
	}

	draw::bitmap(matrix.get_inactive_buffer(), bitmap::subway, 18, 8, 3, pos, 1, 4095, 4095, 4095, true);
	draw::bitmap(matrix.get_inactive_buffer(), bitmap::subway, 18, 8, 3, draw::text(matrix.get_inactive_buffer(), s_alert.data, font::tahoma_9::info, pos + 20, 8, r, g, b) + 2, 1, 4095, 4095, 4095);
}

void tasks::TTCScreen::draw_bus() {
	uint16_t pos = ((timekeeper.current_time / 70) % 228) - 45;
	if (pos == 227 - 45) {
		bus_type = rng::get() % 3;
		bus_type += 1;
	}

	switch (bus_state) {
		case 0:
			for (uint8_t i = 0; i < bus_type; ++i)
				draw::bitmap(matrix.get_inactive_buffer(), bitmap::bus, 14, 7, 2, pos + (i * 24), 2, 230_c, 230_c, 230_c, true);
			break;
		case 2:
			pos += 45;
			pos = (pos * pos) % 228;
			pos -= 45;
			[[fallthrough]];
		case 1:
			{
				for (uint8_t i = 0; i < bus_type + (bus_state == 2 ? 2 : 0); ++i)
					draw::bitmap(matrix.get_inactive_buffer(), bitmap::bus, 14, 7, 2, pos + (i * 24), 2, rng::getclr(), rng::getclr(), rng::getclr(), true);
			}
			break;
	}

}

bool tasks::TTCScreen::draw_slot(uint16_t y, const uint8_t * name, uint64_t times[4], bool alert, bool delay) {
	if (!name) return false;
	uint32_t t_pos = ((timekeeper.current_time / 50));
	uint16_t size = draw::text_size(name, font::tahoma_9::info);

	// If the size doesn't need scrolling, don't scroll it.
	if (size < 128) {
		t_pos = 1;
	}
	else {
		// otherwise make it scroll from the right
		t_pos = draw::scroll(t_pos, size);
	}

	int16_t write_pos[4] = {-1};
	int16_t min_pos = 0;
	int64_t scale_v = 8;

	for (int i = 0; i < 4; ++i) {
		if (times[i] < rtc_time) continue;
		uint64_t minutes = ((times[i] - rtc_time) / 60'000);
		if (minutes > 15 && minutes < 32) scale_v = 4;
	}

	for (int i = 0; i < 4; ++i) {
		if (rtc_time > times[i]) break;
		
		// Scale is 8 pixels per minute
		int16_t position = static_cast<int16_t>((times[i] - rtc_time) / (60'000 / scale_v));

		if (position > 128) {
			break;
		}

		if (position < min_pos) {
			position = min_pos;
		}

		write_pos[i] = position + 2;
		char buf[16] = {0};
		uint64_t minutes = ((times[i] - rtc_time) / 60'000);
		snprintf(buf, 16, "%dm", (int)minutes);

		min_pos = position + draw::text_size(buf, font::lcdpixel_6::info);
	}

	if (write_pos[0] < 0) return false;

	draw::text(matrix.get_inactive_buffer(), name, font::tahoma_9::info, t_pos, y + 8, 255_c, 255_c, 255_c);
	draw::rect(matrix.get_inactive_buffer(), 0, y+9, 128, y+10, 20_c, 20_c, 20_c);

	for (int i = 0; i < 4; ++i) {
		if (write_pos[i] < 0) break;
		if (times[i] < rtc_time) break;

		char buf[16] = {0};
		uint64_t minutes = ((times[i] - rtc_time) / 60'000);
		if (minutes > 31) break;
		snprintf(buf, 16, "%dm", (int)minutes);

		if (minutes < 5) {
			draw::text(matrix.get_inactive_buffer(), buf, font::lcdpixel_6::info, write_pos[i], y+16, 255_c, 255_c, 255_c);
		}
		else if (minutes < 13) {
			draw::text(matrix.get_inactive_buffer(), buf, font::lcdpixel_6::info, write_pos[i], y+16, 100_c, 255_c, 100_c);
		}
		else {
			draw::text(matrix.get_inactive_buffer(), buf, font::lcdpixel_6::info, write_pos[i], y+16, 255_c, 70_c, 70_c);
		}
	}

	draw::rect(matrix.get_inactive_buffer(), 0, y+16, 128, y+17, 50_c, 50_c, 50_c);

	return true;
}

bool tasks::TTCScreen::init() {
	if (!(
		servicer.open_slot(slots::TTC_INFO, true, this->s_info) &&
		servicer.open_slot(slots::TTC_NAME_1, true, this->s_n[0]) &&
		servicer.open_slot(slots::TTC_NAME_2, true, this->s_n[1]) &&
		servicer.open_slot(slots::TTC_NAME_3, true, this->s_n[2]) &&
		servicer.open_slot(slots::TTC_TIME_1, true, this->s_t[0]) &&
		servicer.open_slot(slots::TTC_TIME_2, true, this->s_t[1]) &&
		servicer.open_slot(slots::TTC_TIME_3, true, this->s_t[2]) &&
		servicer.open_slot(slots::TTC_TIME_1B, true, this->s_tb[0]) &&
		servicer.open_slot(slots::TTC_TIME_2B, true, this->s_tb[1]) &&
		servicer.open_slot(slots::TTC_TIME_3B, true, this->s_tb[2]) &&
		s_alert.open(slots::TTC_ALERTSTR)
	)) {
		return false;
	}
	name[0] = 't';
	name[1] = 't';
	name[2] = 'c';
	name[3] = ' ';
	bus_type = rng::get() % 3;
	bus_type += 1;
	bus_state = ((rng::get() % 30) == 0);
	if (bus_state) {
		bus_state = (rng::get() % 2) + 1;
	}
	return true;
}

bool tasks::TTCScreen::deinit() {
	s_alert.close();
	if (!(
		servicer.close_slot(this->s_info) &&
		servicer.close_slot(this->s_n[0]) &&
		servicer.close_slot(this->s_n[1]) &&
		servicer.close_slot(this->s_n[2]) &&
		servicer.close_slot(this->s_t[0]) &&
		servicer.close_slot(this->s_t[1]) &&
		servicer.close_slot(this->s_t[2]) &&
		servicer.close_slot(this->s_tb[0]) &&
		servicer.close_slot(this->s_tb[1]) &&
		servicer.close_slot(this->s_tb[2])
	)) {
		return false;
	}
	ready = false;
	return true;
}
