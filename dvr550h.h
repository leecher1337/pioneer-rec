/*****************************************************
 * Definitions for DVR-550H recorder for pioneer_rec *
 *****************************************************/

#define RECORDER		"DVR-550H"
//#define FNTENTRY_MAGIC	0x0CCC2400	// Entries in FNTENTRY list seem to have this magic

#define FNT_OFFSET	BLOCK_SIZE+32+(2*sizeof(ADDRESS))	// Pointer to FNT directory should be at this offset (sector 5)
#define FAT_OFFSET	6*SECT_SIZE+128						// File allocation table offset relative to start
#define FOT_OFFSET	FAT_OFFSET+5*BLOCK_SIZE				// File order table offset relative to start
/* If FOT cannot be found at this offset, you should try searching for a 
 * 0xFF bitmap follwing the fatdir-pointers and at the end of that list, there
 * is an ADDRESS pointing at the FOT
 */

#define FAT_TS		// Indicate that Timestamp is in FAT, not FNT

#pragma pack(1)
/* A [F]ile [N]ame [T]able entry */
typedef struct {
	long unk1;
	DWORD unk2;
	WORD sort;      
	WORD sort2;		
	WORD sort3;		// Maybe real sort?
	WORD channel;	 // Recording source.. <0x80 = Chan number, >0x80 = LINE: 0x80=L1, 0x81=L2, 0x82=L3, 0x83=DV
	BYTE unk5[8];
	char progname[64];// Name of program
	DWORD unk3;
	WORD fotoffset_hi;
	WORD fotoffset;  // Offset in FOTENTRY table
	WORD files_hi;
	WORD files;		 // Number of files in this directory?
	BYTE unk4[0x9C];
} FNTENTRY;

/* A [F]ile [O]rder [T]able entry */
typedef struct {
	BYTE unk1[8];
	WORD fatoffset;		// Offset in FATENTRY table
	WORD nextoffset;	// If Multipart file, offset of next part in this list, 0 on last part
	DWORD unk2;
	BYTE unk3[16];
} FOTENTRY;

/* A [F]ile [A]llocation [T]able entry */
typedef struct {
	TIMESTAMP ts;
	BYTE unk1[12];
	ADDRESS blockdir;	// Block that contains the blocklist the file consists of
	DWORD unk2;
	DWORD filesz;		// Size of file in blocks
	BYTE unk3[24];
	WORD entry;			// Current entry#
	WORD unk4;			// Always 1?
	BYTE unk5[64];
} FATENTRY;
#pragma pack()
