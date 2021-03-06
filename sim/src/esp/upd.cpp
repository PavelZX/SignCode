#include "upd.h"
#include "config.h"
#include <SdFat.h>
#include "common/util.h"
#include <sys/pgmspace.h>

#define memcmp_P memcmp

#define USTATE_NOT_READ 0xff
#define USTATE_NONE 0xfe

#define USTATE_READY_TO_START 0x00
#define USTATE_CURRENT_SENDING_STM 0x01
#define USTATE_WAITING_FOR_FINISH 0x02
#define USTATE_READY_TO_DO_ESP 0x03
#define USTATE_JUST_DID_ESP 0x05

#define USTATE_READY_FOR_WEB 0x10

#define USTATE_READY_FOR_CERT 0x20

extern SdFatSoftSpi<D6, D2, D5> sd;
uint8_t update_state = 0xff;

upd::UpdateKind upd::needed() {
    Serial1.println(F("update file exists..."));
	if (update_state != USTATE_NOT_READ) {
		goto compute;
	}
    // the upd directory exists, check the files
    if (sd.exists("/upd/state.txt")) {
        Serial1.println(F("update file exists..."));
        File f = sd.open("/upd/state.txt");
        update_state = f.parseInt();
        Serial1.println(update_state);
        f.close();

        goto compute;
    }
    else goto none;
none:
    update_state = USTATE_NONE;
    return upd::NO_UPDATE;
compute:
	switch (update_state) {
		case USTATE_READY_FOR_WEB:
			return upd::WEB_UI;
		case USTATE_READY_FOR_CERT:
			return upd::CERTIFICATE_FILE;
		case USTATE_NONE:
			return upd::NO_UPDATE;
		default:
			return upd::FULL_SYSTEM;
	}
}

void upd::update_website() {
	// check for the webui.ar file
	sd.chdir("/upd");

	if (!sd.exists("webui.ar")) {
		Serial1.println(F("tried to update web but no webui.ar file"));

failure:
		sd.chdir("/upd");
		//sd.remove("state.txt");
		sd.chdir();
		return;
	}

	// otherwise, prepare by BLOWING THE OLD SHIT TO LITTE BITS
	
	sd.chdir("/web");

	Serial1.println(F("upd in removeal"));

	sd.remove("page.html");
	sd.remove("page.css");
	sd.remove("page.js");
	sd.remove("page.html.gz");
	sd.remove("page.css.gz");
	sd.remove("page.js.gz");

	if (sd.exists("etag.txt")) {
		sd.remove("etag.txt");
	}

	yield();

	sd.chdir();

	File arF = sd.open("/upd/webui.ar");
	uint8_t magic[8];
	if (arF.read(magic, sizeof(magic)) != sizeof(magic) ||
			memcmp_P(magic, PSTR("!<arch>\n"), sizeof(magic)) ) {
		arF.close();
invalidformat:
		Serial1.println(F("what kind of shit are you trying to pull here? give me a .ar file..."));
		goto failure;
	}

	int files_read = 0;
	while (files_read < 6) {
		if (arF.peek() == '\n') arF.read();
		uint8_t fileHeader[60];
		if (arF.read(fileHeader, sizeof(fileHeader)) != sizeof(fileHeader)) goto failure;
		fileHeader[58] = 0;

		Serial1.write(fileHeader, 12);
		Serial1.println();

		int iH, iC, iJ, iHc, iCc, iJc;
		// check the filename
		if ((iH = memcmp_P(PSTR("page.html/"), fileHeader, 10))
			&& (iC = memcmp_P("page.css/", fileHeader, 9)) && (iJ = memcmp_P(PSTR("page.js/ "), fileHeader, 9)) && 
			(iHc = memcmp_P(PSTR("page.html.gz"), fileHeader, 12))
			&& (iCc = memcmp_P("page.css.gz/", fileHeader, 11)) && (iJc = memcmp_P(PSTR("page.js.gz/"), fileHeader, 10))) {
			Serial1.println(F("unknown entry"));
			goto failure;
		}

		// ok, now get the size
		int length;
		if (1 != sscanf((char *)(fileHeader + 48), "%d", &length) || !length) {
			goto invalidformat;
		}

		const char * tfname;
		if (!iH) tfname = "/web/page.html";
		else if (!iC) tfname = "/web/page.css";
		else if (!iJ) tfname = "/web/page.js";
		else if (!iHc) tfname = "/web/page.html.gz";
		else if (!iCc) tfname = "/web/page.css.gz";
		else if (!iJc) tfname = "/web/page.js.gz";
		else goto failure;
		File target = sd.open(tfname, FILE_WRITE);

		Serial1.printf_P(PSTR("copying to %s: "), tfname);

		{ 
			uint8_t buf[256];
			for (int i = 0; i < length; i += 256) {
				int size_of_tx = (length - i >= 256) ? 256 : (length - i);
				arF.read(buf, size_of_tx);
				if (target.write(buf, size_of_tx) != size_of_tx) {
					Serial1.print("e");
				}
				else {
					target.flush();
					Serial1.print('.');
				}
			}
		}

		Serial1.println(F("done."));

		++files_read;
	}
	arF.close();

	Serial1.println(F("finished ui update."));
	sd.chdir("/upd");
	sd.remove("state.txt");
	sd.remove("webui.ar");
	sd.chdir();
	sd.rmdir("/upd");
}

void upd::update_cacerts() {}

// routines for doing update

void upd::update_system() {}
