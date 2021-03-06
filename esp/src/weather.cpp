#include "weather.h"
#include "config.h"
#include "serial.h"
#include "util.h"
#include "json.h"
#include "stime.h"
#include "wifi.h"
#include "TimeLib.h"
#include <string.h>
#include "vstr.h"

slots::WeatherInfo weather::info;
slots::WeatherTimes weather::suntimes;
char * weather::info_buffer;
uint8_t weather::buffer_size;
char weather::icon[16];
uint64_t weather::time_since_last_update = 0;
float weather::temp_over_day[24];
slots::WeatherStateArrayCode weather::state_data[24];

serial::VStrSender weather_vss, weather_temp_vss;

void weather::init() {
	weather::info_buffer = nullptr;
	weather::buffer_size = 0;

	memset(&weather::info, 0, sizeof(weather::info));
	memset(&weather::suntimes, 0, sizeof(weather::suntimes));
	memset(weather::icon, 0, 16);
	memset(temp_over_day, 0, sizeof(temp_over_day));
	memset(state_data, 0, sizeof(state_data));

	serial::interface.register_handler([](uint16_t data_id, uint8_t * buffer, uint8_t & length){
		if (data_id == slots::WEATHER_STATUS) {
			weather_vss(buffer, length);
			return true;
		}
		else if (data_id == slots::WEATHER_TEMP_GRAPH) {
			weather_temp_vss(buffer, length);
			return true;
		}
		return false;
	});

	serial::interface.register_handler([](uint16_t data_id){
		switch (data_id) {
			case slots::WEATHER_ICON:
				serial::interface.update_data(slots::WEATHER_ICON, (uint8_t *)weather::icon,  strlen(weather::icon));
				break;
			case slots::WEATHER_INFO:
				serial::interface.update_data(slots::WEATHER_INFO, (uint8_t *)&weather::info, sizeof(weather::info));
				break;
			case slots::WEATHER_ARRAY1:
				serial::interface.update_data(slots::WEATHER_ARRAY1, (uint8_t *)weather::state_data, 16);
				break;
			case slots::WEATHER_ARRAY2:
				serial::interface.update_data(slots::WEATHER_ARRAY2, 16 + (uint8_t *)weather::state_data, 8);
				break;
			case slots::WEATHER_TIME_SUN:
				serial::interface.update_data(slots::WEATHER_TIME_SUN, (uint8_t *)&weather::suntimes, sizeof suntimes);
				break;
			default:
				break;
		}
	});
}

bool use_next_hour_summary = false;

void weather_json_cb(json::PathNode ** stack, uint8_t stack_ptr, const json::Value& v) {
		// First, check if we're in the currently block.
		
		if (stack_ptr == 3 && strcmp_P(stack[1]->name, PSTR("currently")) == 0) {
			// Alright, we need to pull out:
			//
			// Current temp
			// Icon (special: if it's partly-cloudy, just mark that)
			
			if (strcmp_P(stack[2]->name, PSTR("apparentTemperature")) == 0 && v.is_number()) {
				// Store the apparent temperature
				weather::info.ctemp = v.as_number();
				Log.printf_P(PSTR("temp = %f\n"), v.float_val);
			}
			else if (strcmp_P(stack[2]->name, PSTR("icon")) == 0 && v.type == json::Value::STR) {
				memset(weather::icon, 0, 16);
				strcpy(weather::icon, v.str_val);

				Log.printf_P(PSTR("wicon = %s\n"), weather::icon);
			}
			else if (strcmp_P(stack[2]->name, PSTR("temperature")) == 0 && v.is_number()) {
				// Store the apparent temperature
				weather::info.crtemp = v.as_number();
				Log.printf_P(PSTR("rtemp = %f\n"), v.float_val);
			}
		}
		else if (stack_ptr == 3 && strcmp_P(stack[1]->name, PSTR("minutely")) == 0) {
			if (strcmp_P(stack[2]->name, PSTR("summary")) == 0 && v.type == json::Value::STR) {
				weather::buffer_size = strlen(v.str_val) + 1;
				weather::info_buffer = (char*)realloc(weather::info_buffer, weather::buffer_size);
				strcpy(weather::info_buffer, v.str_val);
				Log.printf_P(PSTR("summary (minute) = %s\n"), v.str_val);
			}
			else if (strcmp_P(stack[2]->name, PSTR("icon")) == 0 && v.type == json::Value::STR) {
				if ( /* list of things that we should probably show an hourly summary for */
					!strcmp_P(v.str_val, PSTR("rain")) || !strcmp_P(v.str_val, PSTR("snow")) || !strcmp_P(v.str_val, PSTR("sleet"))) {
					use_next_hour_summary = true;
					Log.println("marking summary as hourly");
				}
			}
		}
		else if (stack_ptr == 3 && strcmp_P(stack[1]->name, PSTR("hourly")) == 0) {
			if (strcmp_P(stack[2]->name, PSTR("summary")) == 0 && v.type == json::Value::STR && !use_next_hour_summary) {
				weather::buffer_size = strlen(v.str_val) + 1;
				weather::info_buffer = (char*)realloc(weather::info_buffer, weather::buffer_size);
				strcpy(weather::info_buffer, v.str_val);
				Log.printf_P(PSTR("summary (hour) = %s\n"), v.str_val);
			}
		}
		else if (stack_ptr == 4 && strcmp_P(stack[1]->name, PSTR("hourly")) == 0 && strcmp_P(stack[2]->name, PSTR("data")) == 0 && stack[2]->is_array() &&
				stack[2]->index < 24) {
			if (strcmp_P(stack[3]->name, PSTR("apparentTemperature")) == 0 && v.is_number()) {
				weather::temp_over_day[stack[2]->index] = v.as_number();
			}
			else if (strcmp_P(stack[3]->name, PSTR("icon")) == 0 && v.type == json::Value::STR) {
				// Part 1 of algorithm; assume we get icon first and set upper nybble
				
				if (strncmp(v.str_val, "partly-cloudy", 13) == 0) {
					weather::state_data[stack[2]->index] = slots::WeatherStateArrayCode::PARTLY_CLOUDY;
				}
				else if (strncmp(v.str_val, "clear", 5) == 0) {
					weather::state_data[stack[2]->index] = slots::WeatherStateArrayCode::CLEAR;
				}
				else if (strcmp_P(v.str_val, PSTR("snow")) == 0) {
					weather::state_data[stack[2]->index] = slots::WeatherStateArrayCode::SNOW;
				}
				else if (strcmp_P(v.str_val, PSTR("cloudy")) == 0) {
					weather::state_data[stack[2]->index] = slots::WeatherStateArrayCode::MOSTLY_CLOUDY;
				}
				else if (strcmp_P(v.str_val, PSTR("rain")) == 0) {
					weather::state_data[stack[2]->index] = slots::WeatherStateArrayCode::DRIZZLE;
				}
			}
			else if (strcmp_P(stack[3]->name, PSTR("cloudCover")) == 0 && v.is_number()) {
				// Part 2a: is the thing a cloud?
				if (((uint8_t)weather::state_data[stack[2]->index] & 0xf0) == (uint8_t)slots::WeatherStateArrayCode::CLEAR && weather::state_data[stack[2]->index] != slots::WeatherStateArrayCode::CLEAR) {
					if (v.as_number() < 0.5) {
						weather::state_data[stack[2]->index] = slots::WeatherStateArrayCode::PARTLY_CLOUDY;
					}
					else if (v.as_number() < 0.9) {
						weather::state_data[stack[2]->index] = slots::WeatherStateArrayCode::MOSTLY_CLOUDY;
					}
					else {
						weather::state_data[stack[2]->index] = slots::WeatherStateArrayCode::OVERCAST;
					}
				}
			}
			else if (strcmp_P(stack[3]->name, PSTR("precipIntensity")) ==0 && v.is_number()) {
				// Part 2b: check precip type from icon
				if (((uint8_t)weather::state_data[stack[2]->index] & 0xf0) == (uint8_t)slots::WeatherStateArrayCode::DRIZZLE) {
					if (v.as_number() < 0.4) {
						weather::state_data[stack[2]->index] = slots::WeatherStateArrayCode::DRIZZLE;
					}
					else if (v.as_number() < 2.5) {
						weather::state_data[stack[2]->index] = slots::WeatherStateArrayCode::LIGHT_RAIN;
					}
					else if (v.as_number() < 10) {
						weather::state_data[stack[2]->index] = slots::WeatherStateArrayCode::RAIN;
					}
					else {
						weather::state_data[stack[2]->index] = slots::WeatherStateArrayCode::HEAVY_RAIN;
					}
				}
				else if (((uint8_t)weather::state_data[stack[2]->index] & 0xf0) == (uint8_t)slots::WeatherStateArrayCode::SNOW) {
					if (v.as_number() < 3) {
						weather::state_data[stack[2]->index] = slots::WeatherStateArrayCode::SNOW;
					}
					else {
						weather::state_data[stack[2]->index] = slots::WeatherStateArrayCode::HEAVY_SNOW;
					}
				}
			}
		}
		else if (stack_ptr == 4 && strcmp_P(stack[1]->name, PSTR("daily")) == 0 && stack[2]->is_array() && strcmp_P(stack[2]->name, PSTR("data")) == 0 && stack[2]->index == 0) {
			if (strcmp_P(stack[3]->name, PSTR("apparentTemperatureHigh")) == 0 && v.is_number()) {
				// Store the high apparent temperature
				weather::info.htemp = v.as_number();
				Log.printf_P(PSTR("htemp = %f\n"), v.as_number());
			}
			else if (strcmp_P(stack[3]->name, PSTR("apparentTemperatureLow")) == 0 && v.is_number()) {
				// Store the low apparent temperature
				weather::info.ltemp = v.as_number();
				Log.printf_P(PSTR("ltemp = %f\n"), v.as_number());
			}
			else if (strcmp_P(stack[3]->name, PSTR("sunriseTime")) == 0 && v.type == json::Value::INT) {
				// Get the sunrise time
				weather::suntimes.sunrise = (signtime::to_local(v.int_val) % 86400) * 1000;
			}
			else if (strcmp_P(stack[3]->name, PSTR("sunsetTime")) == 0 && v.type == json::Value::INT) {
				// Get the sunset time
				weather::suntimes.sunset = (signtime::to_local(v.int_val) % 86400) * 1000;
			}
		}
}

json::JSONParser w_parser(weather_json_cb);

void weather::loop() {
	if (now() < 100) return;
	if (time_since_last_update == 0 || (now() - time_since_last_update) > 180) {
		// Do a weather update.
		
		if (!wifi::available()) return;
		if (config::manager.get_value(config::WEATHER_KEY) == nullptr) return;

		char url[128];
		snprintf_P(url, 128, PSTR("/forecast/%s/%s,%s?exclude=alerts&units=ca"), 
				config::manager.get_value(config::WEATHER_KEY),
				config::manager.get_value(config::WEATHER_LAT),
				config::manager.get_value(config::WEATHER_LONG));

		Log.println(url);

		int16_t status_code;
		auto cb = util::download_with_callback("_api.darksky.net", url, status_code); // leading _ indicates https

		if (status_code < 200 || status_code >= 300) {
			util::stop_download();
			return;
		}

		use_next_hour_summary = false;
		memset(weather::state_data, 0, sizeof(weather::state_data));
		if (!w_parser.parse(std::move(cb))) {
			Log.println("that's no good");
		}
		util::stop_download();
		weather_vss.set((uint8_t *)weather::info_buffer, weather::buffer_size);
		weather_temp_vss.set((uint8_t *) weather::temp_over_day, sizeof(weather::temp_over_day));

		time_since_last_update = now();

		serial::interface.update_data(slots::WEATHER_ICON, (uint8_t *)weather::icon,  strlen(weather::icon));
		serial::interface.update_data(slots::WEATHER_INFO, (uint8_t *)&weather::info, sizeof(weather::info));
		serial::interface.update_data(slots::WEATHER_ARRAY1, (uint8_t *)weather::state_data, 16);
		serial::interface.update_data(slots::WEATHER_ARRAY2, 16 + (uint8_t *)weather::state_data, 8);
		serial::interface.update_data(slots::WEATHER_TIME_SUN, (uint8_t *)&weather::suntimes, sizeof suntimes);
	}
}
