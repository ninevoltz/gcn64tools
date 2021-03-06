/**
 * @file mempak.c
 * @brief Mempak Filesystem Routine
 * @ingroup mempak
 */
#include <string.h>
#include <stdio.h>
#include <wchar.h>
#include "mempak.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

/**
 * @defgroup mempak Mempak Filesystem Routines
 * @ingroup controller
 * @brief Managed mempak interface.
 *
 * The mempak system is a subsystem of the @ref controller.  Before attempting to
 * read from or write to a mempak, be sure you have initialized the controller system
 * with #controller_init and verified that you have a mempak in the correct controller
 * using #identify_accessory.
 *
 * To read and write to the mempak in an organized way compatible with official software,
 * first check that the mempak is valid using #validate_mempak.  If the mempack is
 * invalid, it will need to be formatted using #format_mempak.  Once the mempak is
 * considered valid, existing notes can be enumerated using #get_mempak_entry.  To
 * read the data associated with a note, use #read_mempak_entry_data.  To write a
 * new note to the mempak, use #write_mempak_entry_data.  Note that there is no append
 * functionality so if a note is being updated, ensure you have deleted the old note
 * first using #delete_mempak_entry.  Code should be careful to check how many blocks
 * are free before writing using #get_mempak_free_space.
 *
 * @{
 */

/**
 * @name Inode values
 * @{
 */

/** @brief This block is empty */
#define BLOCK_EMPTY         0x03
/** @brief This is the last block in the note */
#define BLOCK_LAST          0x01
/** @brief First valid block that can contain user data */
#define BLOCK_VALID_FIRST   0x05
/** @brief Last valid block that can contain user data */
#define BLOCK_VALID_LAST    0x7F
/** @} */

static void ucs2_to_utf8(wchar_t *src, int len, char *dst)
{
	int c;

	while (len--) {
		c = *src++;

		if (c < 128) {
			*dst++ = c;
		} else if (c < 0x0800) {
			*dst++ = (0xc0 | ((c>>6) & 0x1f));
			*dst++ = (0x80 | (c & 0x3f));
		} else if (c < 0x10000) {
			*dst++ = (0xE0 | ((c>>12) & 0x0f));
			*dst++ = (0x80 | ((c>>6) & 0x3f));
			*dst++ = (0x80 | (c & 0x3f));
		} else {
			*dst++ = (0xF0 | ((c>>18) & 0x07));
			*dst++ = (0x80 | ((c>>12) & 0x3f));
			*dst++ = (0x80 | ((c>>6) & 0x3f));
			*dst++ = (0x80 | (c & 0x3f));
		}
	}
}

/**
 * @brief Read a sector from a mempak
 *
 * This will read a sector from a mempak.  Sectors on mempaks are always 256 bytes
 * in size.
 *
 * @param[in]  controller
 *             The controller (0-3) to read a sector from
 * @param[in]  sector
 *             The sector (0-127) to read from
 * @param[out] sector_data
 *             Buffer to place 256 read bytes of data
 *
 * @retval 0 if reading was successful
 * @retval -1 if the sector was out of bounds or sector_data was null
 * @retval -2 if there was an error reading part of a sector
 */
int read_mempak_sector( mempak_structure_t *pak, int sector, uint8_t *sector_data )
{
    if( sector < 0 || sector >= 128 ) { return -1; }
    if( sector_data == 0 ) { return -1; }

	memcpy(sector_data, pak->data + sector * MEMPAK_BLOCK_SIZE, MEMPAK_BLOCK_SIZE);
#if 0
    /* Sectors are 256 bytes, a mempak reads 32 bytes at a time */
    for( int i = 0; i < 8; i++ )
    {
        if( read_mempak_address( controller, (sector * MEMPAK_BLOCK_SIZE) + (i * 32), sector_data + (i * 32) ) )
        {
            /* Failed to read a block */
            return -2;
        }
    }
#endif
    return 0;
}

/**
 * @brief Write a sector to a mempak
 *
 * This will write a sector to a mempak.  Sectors on mempaks are always 256 bytes
 * in size.
 *
 * @param[in]  controller
 *             The controller (0-3) to write a sector to
 * @param[in]  sector
 *             The sector (0-127) to write to
 * @param[out] sector_data
 *             Buffer containing 256 bytes of data to write to sector
 *
 * @retval 0 if writing was successful
 * @retval -1 if the sector was out of bounds or sector_data was null
 * @retval -2 if there was an error writing part of a sector
 */
int write_mempak_sector( mempak_structure_t *pak, int sector, uint8_t *sector_data )
{
    if( sector < 0 || sector >= 128 ) { return -1; }
    if( sector_data == 0 ) { return -1; }

	memcpy(pak->data + sector * MEMPAK_BLOCK_SIZE, sector_data, 256);
#if 0
    /* Sectors are 256 bytes, a mempak writes 32 bytes at a time */
    for( int i = 0; i < 8; i++ )
    {
        if( write_mempak_address( controller, (sector * MEMPAK_BLOCK_SIZE) + (i * 32), sector_data + (i * 32) ) )
        {
            /* Failed to read a block */
            return -2;
        }
    }
#endif
    return 0;
}

/**
 * @brief Check a mempak header for validity
 *
 * @param[in] sector
 *            A sector containing a mempak header
 * 
 * @retval 0 if the header is valid
 * @retval -1 if the header is invalid
 */
static int __validate_header( uint8_t *sector )
{
    if( !sector ) { return -1; }

    /* Header is first sector */
    if( memcmp( &sector[0x20], &sector[0x60], 16 ) != 0 ) { return -1; }
    if( memcmp( &sector[0x80], &sector[0xC0], 16 ) != 0 ) { return -1; }
    if( memcmp( &sector[0x20], &sector[0x80], 16 ) != 0 ) { return -1; }

    return 0;
}

/**
 * @brief Calculate the checksum over a TOC sector
 *
 * @param[in] sector
 *            A sector containing a TOC
 *
 * @return The 8 bit checksum over the TOC
 */
static uint8_t __get_toc_checksum( uint8_t *sector )
{
    uint32_t sum = 0;

    /* Rudimentary checksum */
    for( int i = 5; i < 128; i++ )
    {
        sum += sector[(i << 1) + 1];
    }

    return sum & 0xFF;
}

/**
 * @brief Check a mempak TOC sector for validity
 *
 * @param[in] sector
 *            A sector containing a TOC
 *
 * @retval 0 if the TOC is valid
 * @retval -1 if the TOC is invalid
 */
static int __validate_toc( uint8_t *sector )
{
    /* True checksum is sum % 256 */
    if( __get_toc_checksum( sector ) == sector[1] )
    {
        return 0;
    }
    else
    {
        return -1;
    }

    return -1;
}

#if 0
/**
 * @brief Convert a N64 mempak character to ASCII
 *
 * @param[in] c
 *            A character read from a mempak entry title
 *
 * @return ASCII equivalent of character read
 */
static char __n64_to_ascii( char c )
{
    /* Miscelaneous chart */
    switch( c )
    {
        case 0x00:
            return 0;
        case 0x0F:
            return ' ';
        case 0x34:
            return '!';
        case 0x35:
            return '\"';
        case 0x36:
            return '#';
        case 0x37:
            return '`';
        case 0x38:
            return '*';
        case 0x39:
            return '+';
        case 0x3A:
            return ',';
        case 0x3B:
            return '-';
        case 0x3C:
            return '.';
        case 0x3D:
            return '/';
        case 0x3E:
            return ':';
        case 0x3F:
            return '=';
        case 0x40:
            return '?';
        case 0x41:
            return '@';
    }

    /* Numbers */
    if( c >= 0x10 && c <= 0x19 )
    {
        return '0' + (c - 0x10);
    }

    /* Uppercase ASCII */
    if( c >= 0x1A && c <= 0x33 )
    {
        return 'A' + (c - 0x1A);
    }

    /* Default to space for unprintables */
    return ' ';
}
#endif

static const wchar_t n64codepoints[] = {
	// 15: " "
	' ','0','1','2','3','4','5','6','7','8','9',
	'A','B','C','D','E','F','G','H','I','J','K',
	'L','M','N','O','P','Q','R','S','T','U','V',
	'W','X','Y','Z','!','"','#','`','*','+',',','-',
	'.','/',':','=','?','@',
	// 。゛゜ァィゥェォ
	0x3002,0x212B,0x212C,0x30A1,0x30A3,0x30A5,0x30A7,0x30A9,
	// ッャュョ
	0x30C3,0x30E3,0x30E5,0x30E7,
	// ヲンアイウエオ
	0x30F2,0x30F3,0x30A2,0x30A4,0x30A6,0x30A8,0x30AA,
	// カキクケコ
	0x30AB,0x30AD,0x30AF,0x30B1,0x30B3,
	// サシスセソ
	0x30B5,0x30B7,0x30B9,0x30BB,0x30BD,
	// タチツテト
	0x30BF,0x30C1,0x30C4,0x30C6,0x30C8,
	// ナニヌネノ
	0x30CA,0x30CB,0x30CC,0x30CD,0x30CE,
	// ハヒフヘホ
	0x30CF,0x30D2,0x30D5,0x30D8,0x30DB,
	// マミムメモ
	0x30DE,0x30DF,0x30E0,0x30E1,0x30E2,
	// ヤユヨ
	0x30E4,0x30E6,0x30E8,
	// ラリルレロ
	0x30E9,0x30EA,0x30EB,0x30EC,0x30ED,
	// ワ
	0x30EF,
	// ガギグゲゴ
	0x30AC,0x30AE,0x30B0,0x30B2,0x30B4,
	// ザジズゼゾ
	0x30B6,0x30B8,0x30BA,0x30BC,0x30BE,
	// ダヂヅデド
	0x30C0,0x30C2,0x30C5,0x30C7,0x30C9,
	// バビブベボ
	0x30D0,0x30D3,0x30D6,0x30D9,0x30DC,
	// パピプペポ
	0x30D1,0x30D4,0x30D7,0x30DA,0x30DD,
};

static uint8_t __ucs2_to_n64(wchar_t c)
{
	int i;
	for (i=0; i<ARRAY_SIZE(n64codepoints); i++) {
		if (n64codepoints[i] == c) {
			return i + 15;
		}
	}
	return 0;
}

static wchar_t __n64_to_ucs2( uint8_t c )
{
	c -= 15;

	if (c < ARRAY_SIZE(n64codepoints)) {
		return n64codepoints[c];
	}

    /* Default to space for unprintables */
    return ' ';
}

#if 0
/**
 * @brief Convert an ASCII character to a N64 mempak character
 *
 * If the character passed in is one that the N64 mempak doesn't support, this
 * function will default to a space.
 *
 * @param[in] c
 *            An ASCII character
 *
 * @return A N64 mempak character equivalent to the ASCII character passed in
 */
static char __ascii_to_n64( char c )
{
    /* Miscelaneous chart */
    switch( c )
    {
        case 0:
            return 0x00;
        case ' ':
            return 0x0F;
        case '!':
            return 0x34;
        case '\"':
            return 0x35;
        case '#':
            return 0x36;
        case '`':
            return 0x37;
        case '*':
            return 0x38;
        case '+':
            return 0x39;
        case ',':
            return 0x3A;
        case '-':
            return 0x3B;
        case '.':
            return 0x3C;
        case '/':
            return 0x3D;
        case ':':
            return 0x3E;
        case '=':
            return 0x3F;
        case '?':
            return 0x40;
        case '@':
            return 0x41;
    }

    /* Numbers */
    if( c >= '0' && c <= '9' )
    {
        return 0x10 + (c - '0');
    }

    /* Uppercase ASCII */
    if( c >= 'A' && c <= 'Z' )
    {
        return 0x1A + (c - 'A');
    }

    /* Default to space for unprintables */
    return 0x0F;
}
#endif

/**
 * @brief Check a region read from a mempak entry for validity
 *
 * @param[in] region
 *            A region read from a mempak entry
 *
 * @retval 0 if the region is valid
 * @retval -1 if the region is invalid
 */
static int __validate_region( uint8_t region )
{
    switch( region )
    {
        case 0x00:
        case 0x37:
        case 0x41:
        case 0x44:
        case 0x45:
        case 0x46:
        case 0x49:
        case 0x4A:
        case 0x50:
        case 0x53:
        case 0x55:
        case 0x58:
        case 0x59:
            /* Acceptible region */
            return 0;
    }

    /* Invalid region */
    return -3;
}

/**
 * @brief Parse a note structure from a TOC
 *
 * Given a note block read from a mempak TOC, parse and return a structure
 * representing the data.
 *
 * @param[in]  tnote
 *             32 bytes read from a mempak TOC
 * @param[out] note
 *             Parsed note structure
 *
 * @retval 0 note was parsed properly
 * @retval -1 parameters were invalid
 * @retval -2 note inode out of bounds
 * @retval -3 note region invalid
 */
//static int __read_note( uint8_t *tnote, entry_structure_t *note )
int mempak_parse_entry( const uint8_t *tnote, entry_structure_t *note )
{
    if( !tnote || !note ) { return -1; }

    /* Easy stuff */
    note->vendor = (tnote[0] << 16) | (tnote[1] << 8) | (tnote[2]);
    note->region = tnote[3];
	memcpy(note->raw_data, tnote, 32);

    /* Important stuff */
    note->game_id = (tnote[4] << 8) | (tnote[5]);
    note->inode = (tnote[6] << 8) | (tnote[7]);

    /* Don't know number of blocks */
    note->blocks = 0;
    note->valid = 0;
    note->entry_id = 255;

    /* Translate n64 to ascii */
    memset( note->wname, 0, sizeof( note->wname ) );

    for( int i = 0; i < 16; i++ )
    {
        note->wname[i] = __n64_to_ucs2( tnote[0x10 + i] );
    }

    /* Find the last position */
    for( int i = 0; i < 17; i++ )
    {
        if( note->wname[i] == 0 )
        {
            /* Here it is! */
            note->wname[i]   = '.';
            note->wname[i+1] = __n64_to_ucs2( tnote[0xC] );
            break;
        }
    }

	memset(note->utf8_name, 0, sizeof(note->utf8_name));
	ucs2_to_utf8(note->wname, 19, note->utf8_name);

    /* Validate entries */
    if( note->inode < BLOCK_VALID_FIRST || note->inode > BLOCK_VALID_LAST )
    {
        /* Invalid inode */
        return -2;
    }

    if( __validate_region( note->region ) )
    {
        /* Invalid region */
        return -3;
    }

    /* Checks out */
    note->valid = 1;
    return 0;
}

/**
 * @brief Create a note structure for a mempak TOC
 *
 * Given a valid note structure, format it for writing to a mempak TOC
 *
 * @param[in]  note
 *             Valid note structure to convert
 * @param[out] out_note
 *             32 bytes ready to be written to a mempak TOC
 *
 * @retval 0 if the note was converted properly
 * @retval -1 if the parameters were invalid
 */
static int __write_note( entry_structure_t *note, uint8_t *out_note )
{
    wchar_t tname[19];
    if( !out_note || !note ) { return -1; }

    /* Start with baseline */
    memset( out_note, 0, 32 );

    /* Easy stuff */
    out_note[0] = (note->vendor >> 16) & 0xFF;
    out_note[1] = (note->vendor >> 8) & 0xFF;
    out_note[2] = note->vendor & 0xFF;

    out_note[3] = note->region;

    /* Important stuff */
    out_note[4] = (note->game_id >> 8) & 0xFF;
    out_note[5] = note->game_id & 0xFF;
    out_note[6] = (note->inode >> 8) & 0xFF;
    out_note[7] = note->inode & 0xFF;

    /* Fields that generally stay the same on official saves */
    out_note[8] = 0x02;
    out_note[9] = 0x03;

    /* Translate ascii to n64 */
    memcpy( tname, note->wname, sizeof( note->wname ) );
    for( int i = 18; i >= 0; i-- )
    {
        if( tname[i] == '.' )
        {
            /* Found extension */
            out_note[0xC] = __ucs2_to_n64( tname[i+1] );

            /* Erase these as they have been taken care of */
            tname[i] = 0;
            tname[i+1] = 0;
        }
    }

    for( int i = 0; i < 16; i++ )
    {
        out_note[0x10 + i] = __ucs2_to_n64( tname[i] );
    }

    return 0;
}

/**
 * @brief Return number of pages a note occupies
 *
 * Given a starting inode and a TOC sector, walk the linked list for a note
 * and return the number of pages/blocks/sectors a note occupies.
 *
 * @param[in] sector
 *            A TOC sector
 * @param[in] inode
 *            A starting inode
 *
 * @retval -2 The file contained free blocks
 * @retval -3 The filesystem was invalid
 * @return The number of blocks in a note
 */
static int __get_num_pages( uint8_t *sector, int inode )
{
    if( inode < BLOCK_VALID_FIRST || inode > BLOCK_VALID_LAST ) { return -1; }

    int tally = 0;
    int last = inode;
    int rcount = 0;

    /* If we go over this, something is wrong */
    while( rcount < 123 )
    {
        switch( sector[(last << 1) + 1] )
        {
            case BLOCK_LAST:
                /* Last block */
                return tally + 1;
            case BLOCK_EMPTY:
                /* Error, can't have free blocks! */
                return -2;
            default:
                last = sector[(last << 1) + 1];
                tally++;

                /* Failed to point to valid next block */
                if( last < BLOCK_VALID_FIRST || last > BLOCK_VALID_LAST ) { return -3; }
                break;
        }

        rcount++;
    }

    /* Invalid filesystem */
    return -3;
}

/**
 * @brief Get number of free blocks on a mempak
 *
 * @param[in] sector
 *            A valid TOC block to examine
 *
 * @return The number of free blocks
 */
static int __get_free_space( uint8_t *sector )
{
    int space = 0;

    for( int i = BLOCK_VALID_FIRST; i <= BLOCK_VALID_LAST; i++ )
    {
        if( sector[(i << 1) + 1] == BLOCK_EMPTY )
        {
            space++;
        }
    }

    return space;
}

/**
 * @brief Get the inode of the n'th block in a note
 *
 * @param[in] sector
 *            A valid TOC sector
 * @param[in] inode
 *            The starting inode of the note
 * @param[in] block
 *            The block offset (starting from 0) to retrieve
 * 
 * @retval -2 if there were free blocks in the file
 * @retval -3 if the filesystem was invalid
 * @return The inode of the n'th block
 */
static int __get_note_block( uint8_t *sector, int inode, int block )
{
    if( inode < BLOCK_VALID_FIRST || inode > BLOCK_VALID_LAST ) { return -1; }
    if( block < 0 || block > 123 ) { return -1; }

    int tally = block + 1;
    int last = inode;

    /* Only going through until we hit the requested node */
    while( tally > 0 )
    {
        /* Count down to zero, when zero is hit, this is the node we want */
        tally--;

        if( !tally )
        {
            return last;
        }
        else
        {
            switch( sector[(last << 1) + 1] )
            {
                case BLOCK_LAST:
                    /* Last block, couldn't find block number */
                    return -2;
                case BLOCK_EMPTY:
                    /* Error, can't have free blocks! */
                    return -2;
                default:
                    last = sector[(last << 1) + 1];

                    /* Failed to point to valid next block */
                    if( last < 5 || last >= 128 ) { return -3; }
                    break;
            }
        }
    }

    /* Invalid filesystem */
    return -3;
}

/**
 * @brief Retrieve the sector number of the first valid TOC found
 *
 * @param[in] controller
 *            The controller (0-3) to inspect for a valid TOC
 *
 * @retval -2 the mempak was not inserted or was bad
 * @retval -3 the mempak was unformatted or the header was invalid
 * @retval 1 the first sector has a valid TOC
 * @retval 2 the second sector has a valid TOC
 */
static int __get_valid_toc( mempak_structure_t *mpk )
{
    /* We will need only one sector at a time */
    uint8_t data[MEMPAK_BLOCK_SIZE];

    /* First check to see that the header block is valid */
    if( read_mempak_sector( mpk, 0, data ) )
    {
        /* Couldn't read header */
        return -2;
    }

    if( __validate_header( data ) )
    {
        /* Header is invalid or unformatted */
        return -3;
    }

    /* Try to read the first TOC */
    if( read_mempak_sector( mpk, 1, data ) )
    {
        /* Couldn't read header */
        return -2;
    }

    if( __validate_toc( data ) )
    {
        /* First TOC is bad.  Maybe the second works? */
        if( read_mempak_sector( mpk, 2, data ) )
        {
            /* Couldn't read header */
            return -2;
        }

        if( __validate_toc( data ) )
        {
            /* Second TOC is bad, nothing good on this memcard */
            return -3;
        }
        else
        {
            /* Found a good TOC! */
            return 2;
        }
    }
    else
    {
        /* Found a good TOC! */
        return 1;
    }
}

/**
 * @brief Return whether a mempak is valid
 *
 * This function will return whether the mempak in a particular controller
 * is formatted and valid.
 *
 * @param[in] controller
 *            The controller (0-3) to validate
 *
 * @retval 0 if the mempak is valid and ready to be used
 * @retval -2 if the mempak is not present or couldn't be read
 * @retval -3 if the mempak is bad or unformatted
 */
int validate_mempak( mempak_structure_t *mpk )
{
    int toc = __get_valid_toc( mpk );

    if( toc == 1 || toc == 2 )
    {
        /* Found a valid TOC */
        return 0;
    }
    else
    {
        /* Pass on return code */
        return toc;
    }
}

/**
 * @brief Read an entry on a mempak
 *
 * Given an entry index (0-15), return the entry as found on the mempak.  If
 * the entry is blank or invalid, the valid flag is cleared.
 *
 * @param[in]  controller
 *             The controller (0-3) from which the entry should be read
 * @param[in]  entry
 *             The entry index (0-15) to read
 * @param[out] entry_data
 *             Structure containing information on the entry
 *
 * @retval 0 if the entry was read successfully
 * @retval -1 if the entry is out of bounds or entry_data is null
 * @retval -2 if the mempak is bad or not present
 */
int get_mempak_entry( mempak_structure_t *mpk, int entry, entry_structure_t *entry_data )
{
    uint8_t data[MEMPAK_BLOCK_SIZE];
    int toc;

    if( entry < 0 || entry > 15 ) { return -1; }
    if( entry_data == 0 ) { return -1; }

    /* Make sure mempak is valid */
    if( (toc = __get_valid_toc( mpk )) <= 0 )
    {
        /* Bad mempak or was removed, return */
        return -2;
    }

    /* Entries are spread across two sectors, but we can luckly grab just one
       with a single mempak read */
	memcpy(data, mpk->data + (3 * MEMPAK_BLOCK_SIZE) + (entry * 32), 32);
#if 0
    if( read_mempak_address( mpk, (3 * MEMPAK_BLOCK_SIZE) + (entry * 32), data ) )
    {
        /* Couldn't read note database */
        return -2;
    }
#endif

    if( mempak_parse_entry( data, entry_data ) )
    {
        /* Note is most likely empty, don't bother getting length */
        return 0;
    }

    /* Grab the TOC sector */
    if( read_mempak_sector( mpk, toc, data ) )
    {
        /* Couldn't read TOC */
        return -2;
    }

    /* Get the length of the entry */
    int blocks = __get_num_pages( data, entry_data->inode );

    if( blocks > 0 )
    {
        /* Valid entry */
        entry_data->blocks = blocks;
        entry_data->entry_id = entry;
        return 0;
    }
    else
    {
        /* Invalid TOC */
        entry_data->valid = 0;
        return 0;
    }
}

/**
 * @brief Return the number of free blocks on a mempak
 *
 * Note that a block is identical in size to a sector.  To calculate the number of
 * bytes free, multiply the return of this function by #MEMPAK_BLOCK_SIZE.
 *
 * @param[in] controller
 *            The controller (0-3) to read the free space from
 *
 * @return The number of blocks free on the memory card or a negative number on failure
 */
int get_mempak_free_space( mempak_structure_t *mpk )
{
    uint8_t data[MEMPAK_BLOCK_SIZE];
    int toc;

    /* Make sure mempak is valid */
    if( (toc = __get_valid_toc( mpk )) <= 0 )
    {
        /* Bad mempak or was removed, return */
        return -2;
    }

    /* Grab the valid TOC to get free space */
    if( read_mempak_sector( mpk, toc, data ) )
    {
        /* Couldn't read TOC */
        return -2;
    }

    return __get_free_space( data );
}

/**
 * @brief Format a mempak
 *
 * Formats a mempak.  Should only be done to wipe a mempak or to initialize
 * the filesystem in case of a blank or corrupt mempak.
 *
 * @param[in] controller
 *            The controller (0-3) to format the mempak on
 *
 * @retval 0 if the mempak was formatted successfully
 * @retval -2 if the mempak was not present or couldn't be formatted
 */
int format_mempak( mempak_structure_t *mpk )
{
    /* Many mempak dumps exist online for users of emulated games to get
       saves that have all unlocks.  Every beginning sector on all these
       was the same, so this is the data I use to initialize the first
       sector */
    uint8_t sector[MEMPAK_BLOCK_SIZE] = { 0x81,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
                            0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,
                            0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,
                            0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f,
                            0xff,0xff,0xff,0xff,0x05,0x1a,0x5f,0x13,
                            0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                            0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
                            0xff,0xff,0x01,0xff,0x66,0x25,0x99,0xcd,
                            0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                            0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                            0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                            0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                            0xff,0xff,0xff,0xff,0x05,0x1a,0x5f,0x13,
                            0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                            0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
                            0xff,0xff,0x01,0xff,0x66,0x25,0x99,0xcd,
                            0xff,0xff,0xff,0xff,0x05,0x1a,0x5f,0x13,
                            0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                            0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
                            0xff,0xff,0x01,0xff,0x66,0x25,0x99,0xcd,
                            0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                            0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                            0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                            0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                            0xff,0xff,0xff,0xff,0x05,0x1a,0x5f,0x13,
                            0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                            0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
                            0xff,0xff,0x01,0xff,0x66,0x25,0x99,0xcd,
                            0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                            0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                            0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                            0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 };

    if( write_mempak_sector( mpk, 0, sector ) )
    {
        /* Couldn't write initial sector */
        return -2;
    }

    /* Write out entry sectors, which can safely be zero */
    memset( sector, 0x0, MEMPAK_BLOCK_SIZE );
    if( write_mempak_sector( mpk, 3, sector ) ||
        write_mempak_sector( mpk, 4, sector ) )
    {
        /* Couldn't write entry sectors */
        return -2;
    }

    /* Go through, insert 'empty sector' marking on all entries */
    for( int i = 0; i < 128; i++ )
    {
        sector[(i << 1) + 1] = BLOCK_EMPTY;
    }

    /* Fix the checksum */
    sector[1] = __get_toc_checksum( sector );

    /* Write out */
    if( write_mempak_sector( mpk, 1, sector ) ||
        write_mempak_sector( mpk, 2, sector ) )
    {
        /* Couldn't write TOC sectors */
        return -2;
    }

    return 0;
}

/**
 * @brief Read the data associated with an entry on a mempak
 *
 * Given a valid mempak entry fetched by get_mempak_entry, retrieves the contents
 * of the entry.  The calling function must ensure that enough room is available in
 * the passed in buffer for the entire entry.  The entry structure itself contains
 * the number of blocks used to store the data which can be multiplied by
 * #MEMPAK_BLOCK_SIZE to calculate the size of the buffer needed.
 *
 * @param[in]  controller
 *             The controller (0-3) to read the entry data from
 * @param[in]  entry
 *             The entry structure associated with the data to be read.  An entry
 *             structure can be fetched based on index using #get_mempak_entry
 * @param[out] data
 *             The data associated with an entry
 *
 * @retval 0 if the entry was successfully read
 * @retval -1 if input parameters were out of bounds or the entry was corrupted somehow
 * @retval -2 if the mempak was not present or bad
 * @retval -3 if the data couldn't be read
 */
int read_mempak_entry_data( mempak_structure_t *mpk, entry_structure_t *entry, uint8_t *data )
{
    int toc;
    uint8_t tocdata[MEMPAK_BLOCK_SIZE];

    /* Some serious sanity checking */
    if( entry == 0 || data == 0 ) { return -1; }
    if( entry->valid == 0 ) { return -1; }
    if( entry->blocks == 0 || entry->blocks > 123 ) { return -1; }
    if( entry->inode < BLOCK_VALID_FIRST || entry->inode > BLOCK_VALID_LAST ) { return -1; }

    /* Grab the TOC sector so we can get to the individual blocks the data comprises of */
    if( (toc = __get_valid_toc( mpk )) <= 0 )
    {
        /* Bad mempak or was removed, return */
        return -2;
    }

    /* Grab the valid TOC to get free space */
    if( read_mempak_sector( mpk, toc, tocdata ) )
    {
        /* Couldn't read TOC */
        return -2;
    }

    /* Now loop through blocks and grab each one */
    for( int i = 0; i < entry->blocks; i++ )
    {
        int block = __get_note_block( tocdata, entry->inode, i );

        if( read_mempak_sector( mpk, block, data + (i * MEMPAK_BLOCK_SIZE) ) )
        {
            /* Couldn't read a sector */
            return -3;
        }
    }

    /* Fetched all blocks successfully */
    return 0;
}

/**
 * @brief Write associated data to a mempak entry
 *
 * Given a mempak entry structure with a valid region, name and block count, writes the
 * entry and associated data to the mempak.  This function will not overwrite any existing
 * user data.  To update an existing entry, use #delete_mempak_entry followed by
 * #write_mempak_entry_data with the same entry structure.
 *
 * @param[in] controller
 *            The controller (0-3) to write the entry and data to
 * @param[in] entry
 *            The entry structure containing a region, name and block count
 * @param[in] data
 *            The associated data to write to to the created entry
 *
 * @retval 0 if the entry was created and written successfully
 * @retval -1 if the parameters were invalid or the note has no length
 * @retval -2 if the mempak wasn't present or was bad
 * @retval -3 if there was an error writing to the mempak
 * @retval -4 if there wasn't enough space to store the note
 * @retval -5 if there is no room in the TOC to add a new entry
 */
int write_mempak_entry_data( mempak_structure_t *mpk, entry_structure_t *entry, uint8_t *data )
{
    uint8_t sector[MEMPAK_BLOCK_SIZE];
    uint8_t tmp_data[32];
    int toc;

    /* Sanity checking on input data */
    if( !entry || !data ) { return -1; }
    if( entry->blocks < 1 ) { return -1; }
    if( __validate_region( entry->region ) ) { return -1; }
    if( wcslen( entry->wname ) == 0 ) { return -1; }

    /* Grab valid TOC */
    if( (toc = __get_valid_toc( mpk )) <= 0 )
    {
        /* Bad mempak or was removed, return */
        return -2;
    }

    /* Grab the valid TOC to get free space */
    if( read_mempak_sector( mpk, toc, sector ) )
    {
        /* Couldn't read TOC */
        return -2;
    }

    /* Verify that we have enough free space */
    if( __get_free_space( sector ) < entry->blocks )
    {
        /* Not enough space for note */
        return -4;
    }

    /* Find blocks in TOC to allocate */
    int tally = entry->blocks;
    uint8_t last = BLOCK_LAST;

    for( int i = BLOCK_VALID_FIRST; i <= BLOCK_VALID_LAST; i++ )
    {
        if( sector[(i << 1) + 1] == BLOCK_EMPTY )
        {
            /* We can use this block */
            tally--;

            /* Point this towards the next block */
            sector[(i << 1) + 1] = last;

            /* This block is now the last block */
            last = i;
        }

        /* If we found all blocks, we can exit out early */
        if( !tally ) { break; }
    }

    if( tally > 0 )
    {
        /* Even though we had free space, couldn't get all blocks? */
        return -4;
    }
    else
    {
        /* Last now contains our inode */
        entry->inode = last;
        entry->valid = 0;
        entry->vendor = 0;

        /* A value observed in most games */
        entry->game_id = 0x4535;
    }

    /* Loop through allocated blocks and write data to sectors */
    for( int i = 0; i < entry->blocks; i++ )
    {
        int block = __get_note_block( sector, entry->inode, i );

        if( write_mempak_sector( mpk, block, data + (i * MEMPAK_BLOCK_SIZE) ) )
        {
            /* Couldn't write a sector */
            return -3;
        }
    }

    /* Find an empty entry to store to */
    for( int i = 0; i < 16; i++ )
    {
        entry_structure_t tmp_entry;

		memcpy(tmp_data, mpk->data + (3 * MEMPAK_BLOCK_SIZE) + (i * 32), 32);
#if 0
        if( read_mempak_address( mpk, (3 * MEMPAK_BLOCK_SIZE) + (i * 32), tmp_data ) )
        {
            /* Couldn't read note database */
            return -2;
        }
#endif
        /* See if we can write to this note */
        mempak_parse_entry( tmp_data, &tmp_entry );
        if( tmp_entry.valid == 0 )
        {
            entry->entry_id = i;
            entry->valid = 1;
            break;
        }
    }

    if( entry->valid == 0 )
    {
        /* Couldn't find an entry */
        return -5;
    }

    /* Update CRC on newly updated TOC */
    sector[1] = __get_toc_checksum( sector );

    /* Write back to alternate TOC first before erasing the known valid one */
    if( write_mempak_sector( mpk, ( toc == 1 ) ? 2 : 1, sector ) )
    {
        /* Failed to write alternate TOC */
        return -2;
    }

    /* Write back to good TOC now that alternate is updated */
    if( write_mempak_sector( mpk, toc, sector ) )
    {
        /* Failed to write alternate TOC */
        return -2;
    }

    /* Convert entry structure to proper entry data */
    __write_note( entry, tmp_data );

    /* Store entry to empty slot on mempak */
	memcpy(mpk->data + (3 * MEMPAK_BLOCK_SIZE) + (entry->entry_id * 32), tmp_data, 32);
#if 0
    if( write_mempak_address( mpk, (3 * MEMPAK_BLOCK_SIZE) + (entry->entry_id * 32), tmp_data ) )
    {
        /* Couldn't update note database */
        return -2;
    }
#endif

    return 0;
}

/**
 * @brief Delete a mempak entry and associated data
 *
 * Given a valid mempak entry fetched by #get_mempak_entry, removes the entry and frees
 * all associated blocks.
 *
 * @param[in] controller
 *            The controller (0-3) to delete the note from
 * @param[in] entry
 *            The entry structure that is to be deleted from the mempak
 *
 * @retval 0 if the entry was deleted successfully
 * @retval -1 if the entry was invalid
 * @retval -2 if the mempak was bad or not present
 */
int delete_mempak_entry( mempak_structure_t *mpk, entry_structure_t *entry )
{
    entry_structure_t tmp_entry;
    uint8_t data[MEMPAK_BLOCK_SIZE];
    int toc;

    /* Some serious sanity checking */
    if( entry == 0 ) { return -1; }
    if( entry->valid == 0 ) { return -1; }
    if( entry->entry_id > 15 ) { return -1; }
    if( entry->inode < BLOCK_VALID_FIRST || entry->inode > BLOCK_VALID_LAST ) { return -1; }

    /* Ensure that the entry passed in matches what's on the mempak */
	memcpy(data, mpk->data + 3 * MEMPAK_BLOCK_SIZE + entry->entry_id * 32, 32);
#if 0
    if( read_mempak_address( mpk, (3 * MEMPAK_BLOCK_SIZE) + (entry->entry_id * 32), data ) )
    {
        /* Couldn't read note database */
        return -2;
    }
#endif

    if( mempak_parse_entry( data, &tmp_entry ) )
    {
        /* Couldn't parse entry, can't be valid */
        return -2;
    }

    if( tmp_entry.inode != entry->inode )
    {
        /* inodes don't match, not the same entry! */
        return -2;
    }

    /* The entry matches, so blank it */
    memset( mpk->data + (3 * MEMPAK_BLOCK_SIZE) + (entry->entry_id * 32), 0, 32 );
#if 0
    memset( data, 0, 32 );
    if( write_mempak_address( mpk, (3 * MEMPAK_BLOCK_SIZE) + (entry->entry_id * 32), data ) )
    {
        /* Couldn't update note database */
        return -2;
    }
#endif
    /* Grab the first valid TOC entry */
    if( (toc = __get_valid_toc( mpk )) <= 0 )
    {
        /* Bad mempak or was removed, return */
        return -2;
    }

    /* Grab the valid TOC to erase sectors */
    if( read_mempak_sector( mpk, toc, data ) )
    {
        /* Couldn't read TOC */
        return -2;
    }

    /* Erase all blocks out of the TOC */
    int tally = 0;
    int done = 0;
    int last = entry->inode;

    /* Don't want to recurse forever if filesystem is corrupt */
    while( tally <= 123 && !done )
    {
        tally++;

        switch( data[(last << 1) + 1] )
        {
            case BLOCK_LAST:
                /* Last block, we are done */
                data[(last << 1) + 1] = BLOCK_EMPTY;

                done = 1;
                break;
            case BLOCK_EMPTY:
                /* Error, can't have free blocks! */
                return -2;
            default:
            {
                int next = data[(last << 1) + 1];
                data[(last << 1) + 1] = BLOCK_EMPTY;
                last = next;

                /* Failed to point to valid next block */
                if( last < 5 || last >= 128 ) { return -3; }
                break;
            }
        }
    }

    /* Update CRC on newly updated TOC */
    data[1] = __get_toc_checksum( data );

    /* Write back to alternate TOC first before erasing the known valid one */
    if( write_mempak_sector( mpk, ( toc == 1 ) ? 2 : 1, data ) )
    {
        /* Failed to write alternate TOC */
        return -2;
    }

    /* Write back to good TOC now that alternate is updated */
    if( write_mempak_sector( mpk, toc, data ) )
    {
        /* Failed to write alternate TOC */
        return -2;
    }

    return 0;
}

/** @} */ /* controller */
