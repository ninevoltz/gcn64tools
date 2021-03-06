/*	gc_n64_usb : Gamecube or N64 controller to USB adapter firmware
	Copyright (C) 2007-2015  Raphael Assenat <raph@raphnet.net>

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
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "raphnetadapter.h"
#include "gcn64lib.h"
#include "mempak.h"
#include "mempak_gcn64usb.h"
#include "hexdump.h"
#include "gcn64_protocol.h"
#include "requests.h"

#define MEMPAK_IO_RETRIES	5

/* pak_address_crc is renamed from __calc_address_crc from from libdragon which is public domain. */

/**
 * @brief Calculate the 5 bit CRC on a mempak address
 *
 * This function, given an address intended for a mempak read or write, will
 * calculate the CRC on the address, returning the corrected address | CRC.
 *
 * @param[in] address
 *            The mempak address to calculate CRC over
 *
 * @return The mempak address | CRC
 */
uint16_t pak_address_crc( uint16_t address )
{
    /* CRC table */
    uint16_t xor_table[16] = { 0x0, 0x0, 0x0, 0x0, 0x0, 0x15, 0x1F, 0x0B, 0x16, 0x19, 0x07, 0x0E, 0x1C, 0x0D, 0x1A, 0x01 };
    uint16_t crc = 0;
	int i;

    /* Make sure we have a valid address */
    address &= ~0x1F;

    /* Go through each bit in the address, and if set, xor the right value into the output */
    for( i = 15; i >= 5; i-- )
    {   
        /* Is this bit set? */
        if( ((address >> i) & 0x1) )
        {
           crc ^= xor_table[i];
        }
    }   

    /* Just in case */
    crc &= 0x1F;

    /* Create a new address with the CRC appended */
    return address | crc;
}

/* adapted from __calc_data_crc is from libdragon which is public domain. */

/**
 * @brief Calculate the 8 bit CRC over a n-byte block of data
 *
 * This function calculates the 8 bit CRC appropriate for checking a 32-byte
 * block of data intended for or retrieved from a mempak.
 *
 * @param[in] data
 *            Pointer to bytes of data to run the CRC over
 *
 * @param[in] n
 *            Number of bytes of data to run the CRC over
 *
 * @return The calculated 8 bit CRC over the data
 */
uint8_t pak_data_crc( const uint8_t *data, int n )
{
    uint8_t ret = 0;
	int i,j;

    for( i = 0; i <= n; i++ )
    {
        for( j = 7; j >= 0; j-- )
        {
            int tmp = 0;

            if( ret & 0x80 )
            {
                tmp = 0x85;
            }

            ret <<= 1;

            if( i < n )
            {
                if( data[i] & (0x01 << j) )
                {
                    ret |= 0x1;
                }
            }

            ret ^= tmp;
        }
    }

    return ret;
}

int gcn64lib_mempak_readBlock(rnt_hdl_t hdl, unsigned char channel, unsigned short addr, unsigned char dst[32])
{
	unsigned char cmd[64];
	//int cmdlen;
	int n;
	uint16_t addr_crc;
	unsigned char crc;

	addr_crc = pak_address_crc(addr);

	cmd[0] = N64_EXPANSION_READ;
	cmd[1] = addr_crc>>8; // Address high byte
	cmd[2] = addr_crc&0xff; // Address low byte

	n = gcn64lib_rawSiCommand(hdl, channel, cmd, 3, cmd, sizeof(cmd));
	if (n != 33) {
		printf("Hey! %d\n", n);
		return -1;
	}

	memcpy(dst, cmd, 0x20);

	crc = pak_data_crc(dst, 32);
	if (crc != cmd[32]) {
		fprintf(stderr, "Bad CRC reading address 0x%04x. Expected 0x%02x, got 0x%02x\n", addr, crc, cmd[32]);
		return -2;
	}

	return 0x20;
}

int gcn64lib_mempak_detect(rnt_hdl_t hdl, unsigned char channel)
{
	unsigned char buf[40];
	int res;
	unsigned short addr = pak_address_crc(0x8000);
	int first_read, second_read;

	buf[0] = N64_GET_CAPABILITIES;
	res = gcn64lib_rawSiCommand(hdl, channel, buf, 1, buf, sizeof(buf));
	if (res < 0) {
		return -1;
	}
	if (res != 3) {
		return -1;
	}
	if (!(buf[2] & 0x01)) {
		printf("No accessory connected\n");
		return -1;
	}

	/* first write 32 0xFEs */
	memset(buf, 0xfe, 32);
	res = gcn64lib_n64_expansionWrite(hdl, channel, addr, buf, 32);
	if (res < 0) {
		return -1;
	}
	if (res != 0xE1) {
		return -1;
	}

	/* Read back (normally zeros) */
	res = gcn64lib_n64_expansionRead(hdl, channel, addr, buf, sizeof(buf));
	if (res < 0) {
		return -1;
	}
	first_read = buf[0];

	/* Now write 32 0x80s */
	memset(buf, 0x80, 32);
	res = gcn64lib_n64_expansionWrite(hdl, channel, addr, buf, 32);
	if (res < 0) {
		return -1;
	}

	/* Normally, read back (0x00 on memory card, 0x80 on rumble pak)
	 *
	 * But this simple detection method (from libdragon) does not detect one of my
	 * memory cards (it looks like a rumble pack).
	 *
	 * But I found out that the values that are read back are just always equal to while
	 * for other hardware, reading after writing the 0xfe values always seem to return 0x00.
	 */
	res = gcn64lib_n64_expansionRead(hdl, channel, addr, buf, sizeof(buf));
	if (res < 0) {
		printf("failed to detect mempak: %d\n", res);
		return -1;
	}
	second_read = buf[0];

	// Values seen here are
	//
	// - Official Nintendo rumble pack: 0x00
	// - Yobo rumble pack: 0x00
	// - Yobo mempak: 0x00
	// - Unknown "super memory card 1000": 0xFE
	//
	if (first_read == 0xfe) {
		printf("super memory card 1000 probably detected\n");
		return 0;
	}

	// Values seen here are
	//
	// - Official Nintendo rumble pack: 0x80
	// - Yobo rumble pack: 0x80
	// - Yobo mempak: 0x00
	// - Unknown "super memory card 1000": 0x80 (but this card is catched above)
	if (second_read == 0x80) {
		return -1; // rumble
	} else {
		return 0;
	}
}

int gcn64lib_mempak_writeBlock(rnt_hdl_t hdl, unsigned char channel, unsigned short addr, const unsigned char data[32])
{
	int res;
	uint8_t data_crc;

	res = gcn64lib_n64_expansionWrite(hdl, channel, pak_address_crc(addr), data, 32);
	if (res < 0) {
		return res;
	}

	data_crc = pak_data_crc(data, 32);

	if (res != data_crc) {
		//fprintf(stderr, "CRC error\n");
		return -1;
	}

//	printHexBuf(data, 32);

	return 0;
}

/**
 * \brief Read a physical mempak
 * \param hdl The Adapter handler
 * \param channel The adapter channel (for multi-port adapters)
 * \param pak Pointer to mempak_structure pointer to store the new mempak
 * \param progressCb Callback to notify read progress (called after each block). The callback can return non-zero to abort.
 * \return 0: Success, -1: No mempak, -2: IO/error, -3: Other errors, -4: Aborted
 */
int gcn64lib_mempak_download(rnt_hdl_t hdl, int channel, mempak_structure_t **mempak, int (*progressCb)(int cur_addr, void *ctx), void *ctx)
{
	mempak_structure_t *pak;
	unsigned short addr;
	int res, try;

	if (!mempak) {
		return -3;
	}

	if (gcn64lib_mempak_detect(hdl, channel)) {
		return -1;
	}

	pak = calloc(1, sizeof(mempak_structure_t));
	if (!pak) {
		return -3;
	}
	pak->file_format = MPK_FORMAT_MPK;

	for (addr = 0x0000; addr < MEMPAK_MEM_SIZE; addr+= 0x20)
	{
		for (try = 0; try < MEMPAK_IO_RETRIES; try++) {
			res = gcn64lib_mempak_readBlock(hdl, channel, addr, &pak->data[addr]);
			if (res == 0x20) {
				break;
			}
		}

		if (try >= MEMPAK_IO_RETRIES) {
			fprintf(stderr, "Error: Short read\n");
			free(pak);
			return -2;
		}

		if (progressCb) {
			if (progressCb(addr, ctx)) {
				return -4;
			}
		}
	}
	*mempak = pak;

	return 0;
}

int gcn64lib_mempak_upload(rnt_hdl_t hdl, int channel, mempak_structure_t *pak, int (*progressCb)(int cur_addr, void *ctx), void *ctx)
{
	unsigned short addr;
	int res, try;

	if (!pak) {
		return -3;
	}
	if (gcn64lib_mempak_detect(hdl, channel)) {
		return -1;
	}

	for (addr = 0x0000; addr < MEMPAK_MEM_SIZE; addr+= 0x20)
	{
		for (try = 0; try < MEMPAK_IO_RETRIES; try++) {
			res = gcn64lib_mempak_writeBlock(hdl, channel, addr, &pak->data[addr]);
			if (res == 0) {
				break;
			}
		}

		if (try >= MEMPAK_IO_RETRIES) {
			fprintf(stderr, "Write error\n");
			return -2;
		}

		if (progressCb) {
			if (progressCb(addr, ctx)) {
				return -4;
			}
		}
	}

	return 0;
}

