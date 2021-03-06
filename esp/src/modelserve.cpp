#include "modelserve.h"
#include "debug.h"
#include "util.h"
#include <Arduino.h>
#include <TimeLib.h>
#include <SdFat.h>
#include "config.h"
#include "serial.h"
#include "common/slots.h"
#include <cmath>

extern SdFatSoftSpi<D6, D2, D5> sd;

namespace modelserve {
	slots::Vec3 *rgb = 0;
	slots::Vec3 *p1, *p2, *p3 = 0;

	uint16_t tricount[2] = {0, 0};
	uint16_t index = 0;
	uint8_t  modelidx = 0;
	time_t last_switch_time = 0;

	bool     modelspresent[3] = {false, false, true};

	const char * modelpaths[] = {
		"/model.bin",
		"/model1.bin"
	};

	void load_next_index_data() {
		if (tricount[modelidx] == 0) return;

		size_t remaining_triangles = std::min(tricount[modelidx] - index, 16);
		size_t bytes_required = remaining_triangles * sizeof(slots::Vec3);
		size_t offset         = 2 + (sizeof(slots::Vec3) * 4) * index;

		Log.println("A:");
		Log.println(bytes_required);
	
		p1 = (slots::Vec3 *)realloc(p1, bytes_required);	
		p2 = (slots::Vec3 *)realloc(p2, bytes_required);	
		p3 = (slots::Vec3 *)realloc(p3, bytes_required);	
		rgb = (slots::Vec3 *)realloc(rgb, bytes_required);	

		File f = sd.open(modelpaths[modelidx]);
		f.seek(offset);

		slots::Vec3 total[remaining_triangles * 4];
		f.read(&total[0], sizeof(slots::Vec3) * remaining_triangles * 4);
		f.close();

		for (int i = 0; i < remaining_triangles; ++i) {
			p1[i] = total[i * 4 + 0];
			p2[i] = total[i * 4 + 1];
			p3[i] = total[i * 4 + 2];
			rgb[i] = total[i * 4 + 3];
		}
	}

	float get_token_value(const char * v, int i) {
		char * temp = strdup(v);
		char * token = strtok(temp, ",");
		int j = i;
		for (;i > 0;--i) token = strtok(nullptr, ",");
		Log.printf_P(PSTR("v: %s, i: %d, t: %s\n"), v, j, token);
		if (token == nullptr) {
			free(temp);
			return std::numeric_limits<float>::quiet_NaN();
		}
		float r = atof(token);
		Log.println(r);
		free(temp);
		return r;
	}

	void send_model_parameters(config::Entry e) {
		Log.printf_P(PSTR("smp: %02x, idx = %d\n"), e, modelidx);
		if (modelidx == 2) return;
		slots::Vec3 v;
		const char * c;
		if (e == config::MODEL_FOCUSES && (c = config::manager.get_value(config::MODEL_FOCUSES))) {
			v.x = get_token_value(c, modelidx * 9 + 0);
			v.y = get_token_value(c, modelidx * 9 + 1);
			v.z = get_token_value(c, modelidx * 9 + 2);

			serial::interface.update_data(slots::MODEL_CAM_FOCUS1, (uint8_t *)&v, sizeof(v));

			float temp = get_token_value(c, modelidx * 9 + 3);
			if (!std::isnan(temp)) {
				v.x = temp;
				v.y = get_token_value(c, modelidx * 9 + 4);
				v.z = get_token_value(c, modelidx * 9 + 5);
			}

			serial::interface.update_data(slots::MODEL_CAM_FOCUS2, (uint8_t *)&v, sizeof(v));

			temp = get_token_value(c, modelidx * 9 + 6);
			if (!std::isnan(temp)) {
				v.x = temp;
				v.y = get_token_value(c, modelidx * 9 + 4);
				v.z = get_token_value(c, modelidx * 9 + 5);
			}

			v.y = get_token_value(c, modelidx * 9 + 7);
			v.z = get_token_value(c, modelidx * 9 + 8);

			serial::interface.update_data(slots::MODEL_CAM_FOCUS3, (uint8_t *)&v, sizeof(v));
		}
		else if (e == config::MODEL_MINPOSES && (c = config::manager.get_value(config::MODEL_MINPOSES))) {
			v.x = get_token_value(c, modelidx * 3 + 0);
			v.y = get_token_value(c, modelidx * 3 + 1);
			v.z = get_token_value(c, modelidx * 3 + 2);

			serial::interface.update_data(slots::MODEL_CAM_MINPOS, (uint8_t *)&v, sizeof(v));
		}
		else if (e == config::MODEL_MAXPOSES && (c = config::manager.get_value(config::MODEL_MAXPOSES))) {
			v.x = get_token_value(c, modelidx * 3 + 0);
			v.y = get_token_value(c, modelidx * 3 + 1);
			v.z = get_token_value(c, modelidx * 3 + 2);

			serial::interface.update_data(slots::MODEL_CAM_MAXPOS, (uint8_t *)&v, sizeof(v));
		}
	}

	void init_load_model(int i) {
		modelidx = i;
		if (i < 2) {
			index = 0;
			load_next_index_data();

			Log.println(F("d1"));
			Log.println(tricount[0]);
			Log.println(tricount[1]);

			// Send model parameters to device
			serial::interface.update_data(slots::MODEL_INFO, (uint8_t *)&tricount[i], sizeof(uint16_t));
			send_model_parameters(config::MODEL_FOCUSES);
			send_model_parameters(config::MODEL_MINPOSES);
			send_model_parameters(config::MODEL_MAXPOSES);
		}
		else {
			uint16_t c = 0;
			serial::interface.update_data(slots::MODEL_INFO, (uint8_t *)&c, sizeof(c));
		}
	}

	bool init_modeldat(int i) {
		if (!sd.exists(modelpaths[i])) {
			Log.println(F("No model.bin on disk"));
			tricount[i] = 0;
			modelspresent[i] = false;

			return false;
		}

		{
			File f = sd.open(modelpaths[i]);
			f.read(&tricount[i], 2);
			f.close();
		}

		Log.printf_P(PSTR("Got %d triangles.\n"), tricount[i]);
		modelspresent[i] = true;

		return true;
	}

	void init() {
		init_modeldat(0);
		init_modeldat(1);

		// check present data
		char * temp = strdup(config::manager.get_value(config::MODEL_ENABLE, "0,0,1"));
		Log.println(temp);
		char * token = temp;
		for (int i = 0; i < 3; ++i) {
			token = strtok(i == 0 ? temp : NULL, ",");
			Log.println(token);
			if (*token == '0') modelspresent[i] = false;
		}
		free(temp);

		modelidx = 2;
		Log.println(F("loaded model data."));

		serial::interface.register_handler([](uint16_t slot_id){
			switch (slot_id) {
				case slots::MODEL_INFO:
					last_switch_time = 0;
					modelidx = 0;
					return;
				case slots::MODEL_CAM_FOCUS1:
				case slots::MODEL_CAM_FOCUS2:
				case slots::MODEL_CAM_FOCUS3:
					send_model_parameters(config::MODEL_FOCUSES);
					return;
				case slots::MODEL_CAM_MAXPOS:
					send_model_parameters(config::MODEL_MAXPOSES);
					return;
				case slots::MODEL_CAM_MINPOS:
					send_model_parameters(config::MODEL_MINPOSES);
				default:
					return;
			}
		});

		serial::interface.register_handler([](uint16_t slot_id, uint8_t * buffer, uint8_t& size){
			if (slot_id == slots::MODEL_RGB) {
				if (modelidx == 2) return false;
				if (index >= tricount[modelidx]) return false;
				
				// send position data too
				serial::interface.update_data(slots::MODEL_XYZ1, (uint8_t *) &p1[index % 16], sizeof(slots::Vec3));
				serial::interface.update_data(slots::MODEL_XYZ2, (uint8_t *) &p2[index % 16], sizeof(slots::Vec3));
				serial::interface.update_data(slots::MODEL_XYZ3, (uint8_t *) &p3[index % 16], sizeof(slots::Vec3));

				// send rgb data
				memcpy(buffer, &rgb[index % 16], sizeof(slots::Vec3));
				size = sizeof(slots::Vec3);

				// update index
				++index;
				if (index % 16 == 0 && index < tricount[modelidx]) {
					load_next_index_data();
					last_switch_time = now();
				}

				return true;
			}
			return false;
		});

		debug::add_command("nextmodel", [](const char * args, char *&buffer, const char *end){
			last_switch_time = 1;
			buffer += snprintf_P(buffer, end - buffer, PSTR("advancing model\n"));
		}, 32);
	}

	void loop() {
		if (now() < 100) return;
		if (last_switch_time == 0) {
			modelidx = 2;
		}
		else if ((now() - last_switch_time) < 90) return;
		last_switch_time = now();
		Log.println(F("here2"));

		int last_idx = modelidx;

		for (int i = 0; i < 3; ++i) {
			++modelidx;
			modelidx %= 3;
			if (modelspresent[modelidx]) break;
		}

		if (modelidx == last_idx) return;

		Log.println(F("here3"));
		init_load_model(modelidx);
	}
}
