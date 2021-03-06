#include "stm32f2xx.h"
#include "stm32f2xx_ll_rcc.h"
#include "nvic.h"
#include "rcc.h"
#include "rng.h"
#include "fonts/latob_15.h"
#include "fonts/dejavu_10.h"
#include "fonts/lcdpixel_6.h"
#include "schedef.h"
#include "srv.h"
#include "tasks/timekeeper.h"
#include "common/bootcmd.h"

// strange parse error - put this last...

#include "draw.h"
#include "tasks/dispman.h"
#include "tasks/debug.h"
#include <cstring>
#include <stdlib.h>
#include <cmath>

#define SKIP_THRESH 5

#ifndef MSIGN_GIT_REV
#define MSIGN_GIT_REV
#endif

matrix_type matrix;

srv::Servicer servicer;
uint64_t rtc_time;

tasks::Timekeeper timekeeper{rtc_time};
tasks::DebugConsole console;
tasks::DispMan dispman;

// Scheduler parameters

// Slots 0-2: display tasks; need to be run before swap_buffers
// Slot  3  : the service interface - only marks as important when data is in buffer; talks to esp
// Slots 4-7: low priority stuff irrelevant of the thing; possibly alarms/etc, sometimes timekeeping
//
// Slot 0 is usually the active display
// Slot 1 is usually the switcher, responsible for managing slot 0
// Slot 2 is an overlay, which can be started in response to a notification
sched::TaskPtr task_list[8] = {nullptr};
uint8_t task_index = 0;

// increments per skipped task, used to make sure task_list run on time
uint8_t skipped_counter[8] = {0};

// true when the display is ready
bool    display_ready = false;

template <typename FB>
void show_test_pattern(uint8_t stage, FB& fb, const char * extra=nullptr) {
	fb.clear();
	draw::text(fb, "MSIGN V3.1" MSIGN_GIT_REV, font::lcdpixel_6::info, 0, 7, 0, 4095, 0);
	draw::text(fb, "STM OK", font::lcdpixel_6::info, 0, 21, 4095, 4095, 4095);
	char buf[5] = {0};
	strncpy(buf, bootcmd_get_bl_revision(), 4);
	draw::text(fb, buf, font::lcdpixel_6::info, draw::text(fb, "BLOAD ", font::lcdpixel_6::info, 0, 14, 4095, 127_c, 0), 14, 4095, 4095, 4095);
	switch (stage) {
		case 1:
			draw::text(fb, "ESP WAIT", font::lcdpixel_6::info, 0, 28, 4095, 0, 0);
			break;
		case 2:
			draw::text(fb, "ESP OK", font::lcdpixel_6::info, 0, 28, 4095, 4095, 4095);
			draw::text(fb, "UPD NONE", font::lcdpixel_6::info, 0, 35, 0, 4095, 0);
			break;
		case 3:
			draw::text(fb, "ESP OK", font::lcdpixel_6::info, 0, 28, 4095, 4095, 4095);
			if (extra) {
				draw::text(fb, extra, font::lcdpixel_6::info, 0, 35, 40_c, 40_c, 4095);
			}
			else {
				draw::text(fb, "UPD INIT", font::lcdpixel_6::info, 0, 35, 40_c, 40_c, 4095);
			}
			break;
		default:
			break;
	}
}

int main() {
	rcc::init();
	nvic::init();
	rng::init();
	matrix.init();
	servicer.init();

	task_list[3] = &servicer;
	task_list[4] = &dispman;
	task_list[5] = &timekeeper;
	task_list[6] = &console;

	show_test_pattern(0, matrix.get_inactive_buffer());
	matrix.swap_buffers();

	// Init esp comms
	while (!servicer.ready()) {
		servicer.loop();
		matrix.display();
		while (matrix.is_active()) {;}
		show_test_pattern(1, matrix.get_inactive_buffer());
		matrix.swap_buffers();
	}

	if (servicer.updating()) {
		// Go into a simple servicer only update mode
		while (true) {
			servicer.loop();
			matrix.display();
			show_test_pattern(3, matrix.get_inactive_buffer(), servicer.update_status());
			while (matrix.is_active()) {servicer.loop();}
			matrix.swap_buffers();
		}

		// Servicer will eventually reset the STM.
	}

	show_test_pattern(2, matrix.get_inactive_buffer());
	matrix.swap_buffers();
	show_test_pattern(2, matrix.get_inactive_buffer());
	matrix.swap_buffers();

	uint16_t finalize_counter = 60;
	while (finalize_counter--) {
		if (finalize_counter == 50) {
			draw::fill(matrix.get_inactive_buffer(), 4095, 0, 0);
			matrix.swap_buffers();
		}
		if (finalize_counter == 34) {
			draw::fill(matrix.get_inactive_buffer(), 0, 4095, 0);
			matrix.swap_buffers();
		}
		if (finalize_counter == 18) {
			draw::fill(matrix.get_inactive_buffer(), 0, 0, 4095);
			matrix.swap_buffers();
		}
		matrix.display();
		while (matrix.is_active()) {;}
		matrix.display();
		while (matrix.is_active()) {;} // render a few frames so the test patters are actually visible
	}

	dispman.init();

	// Main loop of software
	while (true) {
		matrix.display();
		// ... scheduler loop ...
		while (matrix.is_active()) {
			// Are we done?
			if (task_index >= 8) {
				if (display_ready) continue;
				// No, we are starting anew
				task_index = 0;
			}

			// Check if the current task is null, if so skip

			if (task_list[task_index] == nullptr) {
				++task_index;
				if (task_index == 3) display_ready = true;
				continue;
			}

			// Are we in a display slot?

			if (task_index <= 2) {
				// If so, then we need to treat the task as important
				goto run_it;
			}
			else {
				if (!display_ready) {
					// we are running on borrowed time, skip unless important
					if (task_list[task_index]->important()) goto run_it;
					goto skip;
				}
				else {
					// run the task, it is ok
					goto run_it;
				}
			}

skip:
			++skipped_counter[task_index];
			if (skipped_counter[task_index] > SKIP_THRESH) {
				--skipped_counter[task_index];
				goto run_it;
			}
			++task_index;
			continue;
run_it:
			if (skipped_counter[task_index] > 0)
				--skipped_counter[task_index];

			task_list[task_index]->loop();
			if (task_list[task_index]->done()) {
				++task_index;
				if (task_index == 3) display_ready = true;
			}
		}

		if (display_ready) {
			matrix.swap_buffers(); 
			matrix.get_inactive_buffer().clear();
			display_ready = false;
		}
	}
}
