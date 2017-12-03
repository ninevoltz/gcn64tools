/*	gcn64ctl : raphnet adapter management tools
	Copyright (C) 2007-2017  Raphael Assenat <raph@raphnet.net>

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include <stdio.h>
#include <sys/time.h>
#include <string.h>
#include "mempak_stresstest.h"
#include "hexdump.h"
#include "mempak_gcn64usb.h"
#include "uiio.h"

static int fill_pak(rnt_hdl_t hdl, uiio *u, uint8_t v)
{
	int block;
	uint8_t fill[32];
	int res;

	memset(fill, v, sizeof(fill));

	u->cur_progress = 0;
	u->max_progress = 0x8000 - 32;
	u->progressStart(u);

	for (block=0; block<0x8000; block+=32)
	{
		u->cur_progress = block;
		u->update(u);

		res = gcn64lib_mempak_writeBlock(hdl, block, fill);
		if (res < 0) {
			return -1;
		}
	}

	u->progressEnd(u, "OK");
	return 0;
}

static int check_fill(rnt_hdl_t hdl, uiio *u, uint8_t v)
{
	int block;
	uint8_t expected[32];
	uint8_t buf[32];
	int res;

	memset(expected, v, sizeof(expected));

	u->cur_progress = 0;
	u->max_progress = 0x8000 - 32;
	u->progressStart(u);

	for (block=0; block<0x8000; block+=32)
	{
		u->cur_progress = block;
		u->update(u);

		res = gcn64lib_mempak_readBlock(hdl, block, buf);
		if (res < 0) {
			return -1;
		}
		if (memcmp(expected, buf, sizeof(expected))) {
			printf("\n");
			printf("Expected: "); printHexBuf(expected, 32);
			printf("Readback: "); printHexBuf(buf, 32);
			return -2;
		}
	}

	u->progressEnd(u, "OK");
	return 0;
}

int mempak_fill(rnt_hdl_t hdl, int channel, uint8_t pattern, int no_confirm)
{
	uiio *u = getUIIO(NULL);
	int res;

	u->multi_progress = 1;

	if (!no_confirm) {
		if (UIIO_YES != u->ask(UIIO_NOYES, "This test will erase your memory pak. Are you sure?")) {
			printf("Cancelled\n");
			return -1;
		}
	}

	///////////////////////////////////////////
	u->caption = "Test 10: Fill with pattern";
	if (fill_pak(hdl, u, pattern) < 0) {
		u->error("Error writing to mempak");
		return -1;
	}

	///////////////////////////////////////////
	u->caption = "Test 11: Verify fill";
	res = check_fill(hdl, u, pattern);
	if (res < 0) {
		if (res == -1) {
			u->error("read error");
		}
		else if (res == -2) {
			u->error("verify failed");
		} else {
			u->error("unknown error");
		}
		return -1;
	}

	return 0;
}