/*****************************************************
 * Definitions for DVR-520H recorder for pioneer_rec *
 * written by Rudi Lindl  09/2017                    *
 *****************************************************/

#define RECORDER		"DVR-520H"
//#define FNTENTRY_MAGIC	0x0CCC2400	// Entries in FNTENTRY list seem to have this magic

#define FNT_OFFSET	BLOCK_SIZE+32+20					// Pointer to FNT directory should be at this offset (sector 5)
#define FAT_OFFSET	5*SECT_SIZE+128						// File allocation table offset relative to start
#define FOT_OFFSET	0x2AE8								// File order table offset relative to start
/* If FOT cannot be found at this offset, you should try searching for a 
 * 0xFF bitmap follwing the fatdir-pointers and at the end of that list, there
 * is an ADDRESS pointing at the FOT
 */

#pragma pack(1)
/* A [F]ile [N]ame [T]able entry */
typedef struct {
	TIMESTAMP ts;
	long unk1;
	DWORD unk2;
	WORD sort;       // Maybe sorting number of some kind? Often equal to fotoffset
	WORD fotoffset;  // Offset in FOTENTRY table
	WORD files;		 // Number of files in this directory
	WORD channel;	 // Recording source.. <0x80 = Chan number, >0x80 = LINE: 0x80=L1, 0x81=L2, 0x82=L3, 0x83=DV
	char progname[64];// Name of program
	WORD unk3;
	BYTE locked;	// Locked? 01 = locked, 00 = not locked
	BYTE genre;		// 00 = No genre, 01 = Movies, 03 = Sports, 05 = Others, 06 = Children, 07-0B = Free 1..5
	DWORD magic;
	BYTE unk4[32];
} FNTENTRY;

/* A [F]ile [O]rder [T]able entry */
typedef struct {
	BYTE unk1[8];
	WORD fatoffset;		// Offset in FATENTRY table
	WORD nextoffset;	// If Multipart file, offset of next part in this list, 0 on last part
	DWORD unk2;
} FOTENTRY;

/* A [F]ile [A]llocation [T]able entry */
typedef struct {
	BYTE unk1[8];
	ADDRESS blockdir;	// Block that contains the blocklist the file consists of
	BYTE unk2[44];
	DWORD filesz;		// Size of file in blocks
	BYTE unk3[12];
	WORD entry;			// Current entry#
	WORD unk4;			// Always 1?
	BYTE unk5[16];
} FATENTRY;
#pragma pack()