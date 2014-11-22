/*****************************************************************************
 * Pioneer DVR-633H movie file recovery                                      *
 *                                                                           *
 * (C) by leecher@dose.0wnz.at 2014                                          *
 *****************************************************************************
 * Module:      Main application                                             *
 * File:        pioneer_rec.c                                                *
 * Description: Reads the supplied disk image file and tries to reconstruct  *
 *              the movie files from it parsing the homemade (?) Pioneer DVR *
 *              filesystem.                                                  *
 * Version:     1.0                                                          *
 * Info:        http://code.google.com/p/pioneer-rec                         *
 *****************************************************************************/

#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define snprintf _snprintf

#define BLOCK_SIZE		2048		// Data is allocated in 2K blocks
#define SECT_SIZE		512			// A sector always has 512 bytes

#define VOB_MAGIC		0xBA010000	// Start of a MPEG Pack header
#define FNTENTRY_MAGIC	0x0CCC2400	// Entries in FNTENTRY list seem to have this magic
#define SCR_THRESH_X	0x600		// Default threshold SCR stamps on extract
#define SCR_THRESH_M	5			// Default threshold SCR stamps on merge
#define MAX_DIRSRCH		0x5000000	// Heuristic: Search up to this offset for diretory

#define FNT_OFFSET	BLOCK_SIZE+56	// Pointer to FNT directory should be at this offset (sector 5)
#define FAT_OFFSET	5*SECT_SIZE+128	// File allocation table offset relative to start
#define FOT_OFFSET	0x2AE8			// File order table offset relative to start
/* If FOT cannot be found at this offset, you should try searching for a 
 * 0xFF bitmap follwing the fatdir-pointers and at the end of that list, there
 * is an ADDRESS pointing at the FOT
 */

#pragma pack(1)

// This seems to be an address on disk consising of a block number and unknown additional bytes
typedef struct {
	DWORD block;	// -> Points to the Block on disk
	DWORD unk1;
} ADDRESS;

/* A [F]ile [N]ame [T]able entry */
typedef struct {
	WORD yy;	// Year
	BYTE mm;	// Month
	BYTE dd;	// Day
	BYTE hh;	// Hour
	BYTE mi;	// Minute
	BYTE ss;	// Second
	BYTE pad;
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
	BYTE unk1[12];
	ADDRESS blockdir;	// Block that contains the blocklist the file consists of
	BYTE unk2[28];
	DWORD filesz;		// Size of file in blocks
	BYTE unk3[28];
	WORD entry;			// Current entry#
	WORD unk4;			// Always 1?
	BYTE unk5[12];
} FATENTRY;

/* A list ob blocks the file consists of */
typedef struct {
	DWORD block;	// Block that contains the data
	WORD blocks;	// Number of blocks the frame consists of, remove highest bit (keyframe?)
	WORD unk;
} BLISTENTRY;

#pragma pack()

/* Constants */
#define FNTDIR_ENTRIES   (SECT_SIZE/sizeof(ADDRESS))
#define BLOCKDIR_ENTRIES (BLOCK_SIZE/sizeof(ADDRESS))
#define FATDIR_ENTRIES   (BLOCK_SIZE/sizeof(ADDRESS))
#define FOTDIR_ENTRIES   ((1<<(sizeof(WORD)*8))/FOT_ENTRIES)
#define FOT_ENTRIES		 (BLOCK_SIZE/sizeof(FOTENTRY))
#define FNT_ENTRIES      (BLOCK_SIZE/sizeof(FNTENTRY))
#define FAT_ENTRIES		 (BLOCK_SIZE/sizeof(FATENTRY))
#define BLIST_ENTRIES    (BLOCK_SIZE/sizeof(BLISTENTRY))

/* Prototypes */
static __inline unsigned long B2N_32(unsigned long x) {
  __asm {
    mov eax, x
    bswap eax
  }
}
static __inline unsigned short B2N_16(unsigned short x) {
  __asm {
    mov ax, x
    xchg ah, al
	cwde
  }
}
typedef int (WINAPI *fpPathCleanupSpec)(PCWSTR pszDir, PWSTR pszSpec);
static void ShowError (void);
static int myPathCleanupSpec(PWSTR pszSpec);
static void dumpFNTEntry(char *pszDstDir, int idx, FNTENTRY *fnt);
static void printFNTEntry(char *pszDstDir, WCHAR *wszDstDir, fpPathCleanupSpec _PathCleanupSpec, int idx, FNTENTRY *fnt);
static BOOL dumpFile(HANDLE fp, HANDLE fpOut, FATENTRY *fat);

/****************************************************************************
 *                                P U B L I C                               *
 ****************************************************************************/

/* Show usage information for commandline program
 *
 * Params:
 *	pszCmd		- Filename to show, usually argv[0]
 * Returns:
 *	EXIT_FAILURE
 */
int usage(char *pszCmd)
{
	printf ("Usage: %s [-s<x|m> <SCR threshold>] [-o <Offset>] [-c <Start-Cnt>]\n"
		"\t<-l> <src file>\n"
		"\t<-d|-x|-p|-L> <src file> <dest dir>\n"
		"\t<-m> <dest dir>\n\n"
		"Options:\n"
		"\t-s\tSpecifies the System clock Reference (SCR) threshold that\n"
		"\t\tconsiders consecutive MPEG1-chunks to belong together. Normally\n"
		"\t\tyou don't need to change these values. x and m specifies the\n"
		"\t\tthreshold for the given operation:\n"
		"\t-sx\tSpecifies threshold for -x (extract), Default: %d\n"
		"\t-sm\tSpecifies threshold for -m (merge), Default: %d\n"
		"\t-o\tSpecifies offset in <src file> where to start search, Def.: 0\n"
		"\t-c\tSpecifies start counter of MPEG-File naming (extract), Def.: 0\n\n"
		"Operations (mandatory):\n"
		"\t-l\tList names of recordings found in <src file>\n"
		"\t-L\tCreate directories for found recordings in <dest dir>\n"
		"\t-d\tProcess directory list and dump all movies from <src file>\n"
		"\t\tto <dest dir>\n"
		"\t-x\tExtract MPEG-Chunks from <src file> to <dest dir>\n"
		"\t-m\tMerge MPEG-Chunks by SCR in <dest dir>\n"
		"\t-p\tProcess (Extract & Merge)\n\n"
		"If your directory structure and image isn't garbled, you should use -d\n"
		"In case your image is severely damaged, you can try -p instead.\n\n"
		"Example: %s -d pioneer.img f:\\dump\n", pszCmd, SCR_THRESH_X, SCR_THRESH_M, 
		pszCmd);
	return EXIT_FAILURE;
}

//---------------------------------------------------------------------------

/* Extracts movie chunks aligned at CHUNK_SIZE from image to files
 *
 * Params:
 *	pszSrc		- Image file to parse
 *	pszDstDir	- Destination directory where to put the extracted chunks to
 *	dwOffset	- Offset where to start parsing in file, usually 0
 *	dwThresh	- Maximum threshold to consider 2 consecutive MPEG-segmets as
 *				  joinable.
 * Returns:
 *	TRUE on success, FALSE on failure
 */
BOOL extract(char *pszSrc, char *pszDstDir, DWORD dwOffset, DWORD fno, DWORD dwThresh)
{
	HANDLE fp, fpout=NULL;
	LARGE_INTEGER imgsz, offset;
	DWORD read;
	unsigned long lastvob=0, vob;
	BYTE buf[BLOCK_SIZE];
	char outfile[MAX_PATH], *pszAction="Scanning";

	if (access(pszDstDir, 2))
	{
		fprintf(stderr, "Cannot write to destination dir %s: ", pszDstDir, strerror(errno));
		return FALSE;
	}
	if ((fp=CreateFile(pszSrc, GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL)) == INVALID_HANDLE_VALUE)
	{
		fprintf(stderr, "Cannot open source file %s: ", pszSrc);
		ShowError();
		return FALSE;
	}
	offset.QuadPart = dwOffset;
	if (dwOffset) SetFilePointer(fp,dwOffset,NULL,FILE_BEGIN);
	imgsz.LowPart=GetFileSize(fp,&imgsz.HighPart);
	while(ReadFile(fp,buf,sizeof(buf),&read,NULL) && read)
	{
		if (offset.QuadPart%100==0) printf ("\r%s %X%08X/%X%08X (%0d%%)...", pszAction, offset.HighPart, offset.LowPart, 
			imgsz.HighPart, imgsz.LowPart, (int)(((double)offset.QuadPart/(double)imgsz.QuadPart)*100));
		if (*((unsigned long*)buf)==VOB_MAGIC)
		{
			pszAction="Dumping";
			vob=B2N_32(*((unsigned long*)&buf[4]));
			if (vob>lastvob+dwThresh || vob<lastvob || lastvob==0)
			{
				if (fpout) CloseHandle(fpout);
				snprintf(outfile, sizeof(outfile), "%s\\%08d.mpg", pszDstDir, fno);
				printf ("\rNew video stream found at offset %X%08X ", offset.HighPart, offset.LowPart);
				while ((fpout=CreateFile(outfile, GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, CREATE_NEW, 0, NULL)) == INVALID_HANDLE_VALUE)
				{
					if (GetLastError() != ERROR_FILE_EXISTS)
					{
						fprintf(stderr, "Cannot write to destination file %s: ", outfile);
						ShowError();
						CloseHandle(fp);
						return FALSE;
					}
					snprintf(outfile, sizeof(outfile), "%s\\%08d.mpg", pszDstDir, ++fno);
				}
				printf ("-> %08d.mpg\n", fno);
				fno++;
			}
			if (!WriteFile(fpout,buf,read,&read,NULL))
			{
				fprintf(stderr, "Cannot append to destination file %s: ", outfile);
				ShowError();
				CloseHandle(fpout);
				CloseHandle(fp);
				return FALSE;
			}
			lastvob=vob;
		} else pszAction="Scanning";
		offset.QuadPart += read;
	}
	printf ("\r%s %X%08X/%X%08X (100%%)...", pszAction, offset.HighPart, offset.LowPart);
	if (fpout) CloseHandle(fpout);
	CloseHandle(fp);
	printf ("Done.\n");
	return TRUE;
}

//---------------------------------------------------------------------------

/* Merges together the mpeg-chunkfiles found in pszDstDir to create 
 * consecutive movie chunks out of it.
 * Use with care!
 *
 * Params:
 *	pszDstDir	- Directory that contains all the movie chunks that will be
 *				  merged together.
 *	dwStep		- Threshold, chunks with SCR >4 && <dwThreshold are merged 
 *				  together.
 * Returns:
 *	TRUE on success, FALSE on failure
 */
BOOL merge(char *pszDstDir, DWORD dwStep)
{
	WIN32_FIND_DATA find;
	HANDLE hFind, hFile;
	HANDLE hFind2, hFile2;
	WIN32_FIND_DATA find2;
	LARGE_INTEGER imgsz, offset;
	char szPath[MAX_PATH], *pFile, buf[32768];
	DWORD vob, vob2, read;
	BOOL bRestart;

	do
	{
		pFile=szPath+sprintf(szPath, "%s\\*.*", pszDstDir)-3;
		if ((hFind=FindFirstFile(szPath, &find)) == INVALID_HANDLE_VALUE)
		{
			fprintf(stderr, "Cannot parse directory %s: ", pszDstDir);
			ShowError();
			return FALSE;
		}
		bRestart=FALSE;
		do
		{
			sprintf(pFile, find.cFileName);
			if ((hFile=CreateFile(szPath, GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, 
				OPEN_EXISTING, 0, NULL)) != INVALID_HANDLE_VALUE)
			{
				SetFilePointer(hFile, -0x800+4, NULL, FILE_END);
				vob=ReadFile(hFile,&vob,sizeof(vob),&read,NULL)?B2N_32(vob):0;
				sprintf(pFile, "*.*");
				if ((hFind2=FindFirstFile(szPath, &find2)) != INVALID_HANDLE_VALUE)
				{
					do
					{
						sprintf(pFile, find2.cFileName);
						printf ("\rMatching %s/%s...", find.cFileName, find2.cFileName);
						if ((hFile2=CreateFile(szPath, GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, 
							OPEN_EXISTING, 0, NULL)) != INVALID_HANDLE_VALUE)
						{
							SetFilePointer(hFile2, 4, NULL, FILE_BEGIN);
							vob2=ReadFile(hFile2,&vob2,sizeof(vob2),&read,NULL)?B2N_32(vob2):0;
							if (vob2>=vob+4 && vob2<=vob+dwStep)
							{
								bRestart = TRUE;
								SetFilePointer(hFile, 0, NULL, FILE_END);
								SetFilePointer(hFile2, 0, NULL, FILE_BEGIN);
								imgsz.LowPart=GetFileSize(hFile2,&imgsz.HighPart);
								offset.QuadPart = 0;
								while(ReadFile(hFile2,buf,sizeof(buf),&read,NULL) && read)
								{
									offset.QuadPart += read;
									if (offset.QuadPart%100==0) 
										printf ("\rAppending %s to %s...%d%%", find2.cFileName, find.cFileName, 
										(int)(((double)offset.QuadPart/(double)imgsz.QuadPart)*100));
									WriteFile(hFile, buf, read, &read, NULL);
								}
								printf ("\rAppending %s to %s...Done.\n", find2.cFileName, find.cFileName);
							}
							CloseHandle(hFile2);
							if (bRestart) DeleteFile(szPath);
						}
					}
					while (!bRestart && FindNextFile(hFind2, &find2));
					FindClose(hFind2);
				}
				CloseHandle(hFile);
			}
		}
		while (!bRestart && FindNextFile(hFind, &find));
		FindClose(hFind);
	}
	while (bRestart);
	printf ("No more matches found. -> Done\n");
	return TRUE;
}

//---------------------------------------------------------------------------

/* Parses the directory tree to show the names of the recorded movies.
 * It is also able to dump the movies to a destination directory or to just
 * create the destination directory tree based on the directory depending
 * on the given mode setting:
 * To just list the directory, either use MODE_HEUR or MODE_USEFS and set
 * pszDstDir to NULL,
 * To create directory entries, use one of these 2 modes, but also specify
 * pszDstDir.
 * To create the destination directories and extract the movie files into 
 * them, use MODE_EXTRACT and specify pszDstDir.
 *
 * Params:
 *	pszSrc		- Fielname of source image file
 *	pszDstDir	- If creating Dirlist and/or dumping movies, destination
 *				  directory where extracted data should be put to.
 *  dwThresh	- Offset of last position where to search directory 
 *				  information on MODE_HEUR.
 *				  Usually you should put MAX_DIRSRCH here.
 *  dwMode		- Mode parameter specifying the action to take. See the
 *				  #defines below for a description.
 * Returns:
 *	TRUE on success, FALSE on failure.
 */
#define MODE_HEUR		0	/* Heuristic mode, just scan for Dir entries */
#define MODE_USEFS		1	/* Use Filesystem info instead of dumb scanning */
#define MODE_EXTRACT	2	/* Extract files using Filesytem, requires pszDstDir */
BOOL dir(char *pszSrc, char *pszDstDir, DWORD dwThresh, DWORD dwMode)
{
	HANDLE fp, fpOut;
	HMODULE hShell;
	LARGE_INTEGER imgsz, offset;
	DWORD read;
	WCHAR wszDstDir[MAX_PATH];
	fpPathCleanupSpec _PathCleanupSpec = NULL;
	int i;
	FNTENTRY fntentry[FNT_ENTRIES], *pfntentry;
	ADDRESS fntaddr;

	if (pszDstDir)
	{
		HANDLE hAccessToken;
		LUID luidPrivilege;

		if (access(pszDstDir, 2))
		{
			fprintf(stderr, "Cannot write to destination dir %s: ", pszDstDir, strerror(errno));
			return FALSE;
		}
		MultiByteToWideChar(CP_ACP,0,pszDstDir,-1,wszDstDir,MAX_PATH);
		if (OpenProcessToken (GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hAccessToken))
		{
			if (LookupPrivilegeValue (NULL, SE_BACKUP_NAME, &luidPrivilege)) 
			{
				TOKEN_PRIVILEGES tpPrivileges;
				tpPrivileges.PrivilegeCount = 1;
				tpPrivileges.Privileges[0].Luid = luidPrivilege;
				tpPrivileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
				AdjustTokenPrivileges (hAccessToken, FALSE, &tpPrivileges, 
									   0, NULL, NULL);
			}
		}
	}
	if ((fp=CreateFile(pszSrc, GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, 
		OPEN_EXISTING, 0, NULL)) == INVALID_HANDLE_VALUE)
	{
		fprintf(stderr, "Cannot open source file %s: ", pszSrc);
		ShowError();
		return FALSE;
	}

	/* Function is only available starting with Windows XP, so we must not depend
	 * on it to be compatible with Win9x/NT
	 */
	if (hShell=LoadLibrary("shell32.dll"))
		_PathCleanupSpec=(fpPathCleanupSpec)GetProcAddress(hShell, "PathCleanupSpec");
	
	SetFilePointer(fp,FNT_OFFSET,NULL,FILE_BEGIN);
	if (!ReadFile(fp,&fntaddr,sizeof(fntaddr),&read,NULL) || !read)
	{
		fprintf (stderr, "Cannot read FNT address @%X\n", FNT_OFFSET);
		ShowError();
		return FALSE;
	}
	offset.QuadPart = fntaddr.block?B2N_32(fntaddr.block):0x4008;
	offset.QuadPart *= BLOCK_SIZE;
	if (dwThresh) imgsz.QuadPart =dwThresh;
	else imgsz.LowPart=GetFileSize(fp,&imgsz.HighPart);
	SetFilePointer(fp,offset.LowPart, &offset.HighPart,FILE_BEGIN);

	if (dwMode)
	{
		ADDRESS fntdir[FNTDIR_ENTRIES], fatdir[FATDIR_ENTRIES], fotdir[FOTDIR_ENTRIES];
		FATENTRY fatentry[FAT_ENTRIES], *pfatentry;
		FOTENTRY fotentry[FOT_ENTRIES], *pfotentry;
		int j, k, pfx, fatoffset, fotoffset, ftoffs, files;

		/* Accurately parse directory structure and extract videos based on
		 * filesystem information
		 */

		/* Read a sector (file name table directory) */
		if (!ReadFile(fp,&fntdir,sizeof(fntdir),&read,NULL) || !read)
		{
			fprintf (stderr, "Cannot read FNT Directory info @%X%08X: ", offset.HighPart, offset.LowPart);
			ShowError();
			return FALSE;
		}

		/* Go to FATENTRY list (forward 4 sectors) */
		offset.QuadPart += FAT_OFFSET;
		if (!SetFilePointer(fp,offset.LowPart, &offset.HighPart,FILE_BEGIN) ||
			!ReadFile(fp,&fatdir,sizeof(fatdir),&read,NULL) || !read)
		{
			fprintf (stderr, "Cannot read FATENTRY list @%X%08X: ", offset.HighPart, offset.LowPart);
			ShowError();
			return FALSE;
		}

		/* Now find block number of file order table */
		offset.QuadPart += (FOT_OFFSET)-(FAT_OFFSET);
		if (!SetFilePointer(fp,offset.LowPart, &offset.HighPart,FILE_BEGIN) ||
			!ReadFile(fp,&fotdir,sizeof(fotdir),&read,NULL) || !read)
		{
			fprintf (stderr, "Cannot read File order table directory @%X%08X: ", offset.HighPart, offset.LowPart);
			ShowError();
			return FALSE;
		}


		/* Now iterate over file name directory (program names) */
		for (i=0, pfx=0; fntdir[i].block && i<FNTDIR_ENTRIES; i++)
		{
			offset.QuadPart=B2N_32(fntdir[i].block);
			offset.QuadPart *= BLOCK_SIZE;
			if (SetFilePointer(fp,offset.LowPart, &offset.HighPart,FILE_BEGIN) &&
				ReadFile(fp,&fntentry,sizeof(fntentry),&read,NULL) && read)
			{
				/* Iterate over programs */
				for (j=0; fntentry[j].yy && j<FNT_ENTRIES; j++, pfx++)
				{
					pfntentry = &fntentry[j];
					printFNTEntry(pszDstDir, wszDstDir, _PathCleanupSpec, pfx, pfntentry);
					files = B2N_16(pfntentry->files);
					if (files>1) printf ("%d files.\n", files);

					/* Iterate over files the program consists of */
					fotoffset = B2N_16(pfntentry->fotoffset);
					for (k=0; k<files && fotoffset; k++)
					{
						fotoffset--;
						if ((ftoffs = fotoffset/FOT_ENTRIES)< FOTDIR_ENTRIES &&
							fotdir[ftoffs].block)
						{
							offset.QuadPart = B2N_32(fotdir[ftoffs].block);
							offset.QuadPart *= BLOCK_SIZE;
							if (SetFilePointer(fp,offset.LowPart, &offset.HighPart,FILE_BEGIN) &&
								ReadFile(fp,&fotentry,sizeof(fotentry),&read,NULL) && read)
							{
								pfotentry = &fotentry[fotoffset%FOT_ENTRIES];
								fatoffset = B2N_16(pfotentry->fatoffset)-1;
								if ((ftoffs = fatoffset/FAT_ENTRIES)< FATDIR_ENTRIES &&
									fatdir[ftoffs].block)
								{
									/* Seek to FAT entry for given file */
									offset.QuadPart=B2N_32(fatdir[ftoffs].block);
									offset.QuadPart *= BLOCK_SIZE;
									if (SetFilePointer(fp,offset.LowPart, &offset.HighPart,FILE_BEGIN) &&
										ReadFile(fp,&fatentry,sizeof(fatentry),&read,NULL) && read)
									{
										pfatentry = &fatentry[fatoffset%FAT_ENTRIES];
										offset.QuadPart = B2N_32(pfatentry->filesz);
										printf ("Size: %d blocks ", offset.LowPart);
										offset.QuadPart *= BLOCK_SIZE / 1024;
										offset.QuadPart /= 1024;
										printf ("(%d MB)\n", offset.LowPart);

										if (pszDstDir && dwMode == MODE_EXTRACT)
										{
											char outfile[MAX_PATH];

											/* Seek to block list directory */
											snprintf(outfile, sizeof(outfile), "%s\\[%03d]%s\\%08d.mpg", pszDstDir, pfx, pfntentry->progname, k);
											if ((fpOut=CreateFile(outfile, GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, 
												CREATE_ALWAYS, 0, NULL)) != INVALID_HANDLE_VALUE)
											{
												/* And finally dump the files associated with the program */
												dumpFile(fp,fpOut,pfatentry);
												CloseHandle(fpOut);

												/* Ensure to set date/time correctly for the files afterwards */
												dumpFNTEntry(pszDstDir, pfx, pfntentry);
											}
											else
											{
												fprintf (stderr, "Cannot write to file %s: ", outfile);
												ShowError();
											}
										}
									}
									else
									{
										fprintf (stderr, "Cannot seek to fatentry #%d @ offset %X%08X: ", ftoffs, offset.HighPart, offset.LowPart);
										ShowError();
										break;
									}
								}
								else
									fprintf(stderr, "Invalid FAT entry (#%d)\n", ftoffs);
								fotoffset=B2N_16(pfotentry->nextoffset);
							}
							else
							{
								fprintf (stderr, "Cannot seek to file order table entry #%d @block %X: ", ftoffs, B2N_32(fotdir[ftoffs].block));
								ShowError();
								break;
							}

						}
						else
							fprintf(stderr, "Offset %d in FOT doesn't point to a correct directory.\n", fotoffset);
					}
				}
			}
			else
			{
				fprintf(stderr, "Cannot read FNTENTRY @%X%08X: ", offset.HighPart, offset.LowPart);
				ShowError();
			}
		}
	}
	else
	{
		/* Just do a dumb signature match scan to find directory structure.
		 * i.e. to be used if Filesystem information is partly destroyed
		 */
		while(ReadFile(fp,&fntentry,sizeof(fntentry),&read,NULL) && read && offset.QuadPart<imgsz.QuadPart)
		{
			if (offset.QuadPart%100==0) printf ("\r%s %X%08X/%X%08X (%0d%%)...", "Scanning", offset.HighPart, offset.LowPart, 
				imgsz.HighPart, imgsz.LowPart, (int)(((double)offset.QuadPart/(double)imgsz.QuadPart)*100));
			for (i=0; fntentry[i].yy && fntentry[i].magic == FNTENTRY_MAGIC && i<FNT_ENTRIES; i++)
				printFNTEntry(pszDstDir, wszDstDir, _PathCleanupSpec, i, &fntentry[i]);
			offset.QuadPart += read;
		}
	}
	CloseHandle(fp);
	printf ("Done.\n");
	if (hShell) FreeLibrary(hShell);
 	return TRUE;
}

/****************************************************************************
 *                                S T A T I C                               *
 ****************************************************************************/

/* Prints the last error that occured to stderr */
static void ShowError (void)
{
	char szMsg[1024];

	FormatMessage (FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError(),
		MAKELANGID (LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR)szMsg, sizeof(szMsg), NULL);
	fprintf (stderr, "%s\n", szMsg);
}

//---------------------------------------------------------------------------

/* Cleans out invalid characters from program name so that a directory with
 * a valid name can be created
 *
 * Params:
 *	pszSpec		- Directory that contains all the movie chunks that will be
 *				  merged together.
 *	dwStep		- Threshold, chunks with SCR >4 && <dwThreshold are merged 
 *				  together.
 * Returns:
 *	TRUE on success, FALSE on failure
 */
static int myPathCleanupSpec(PWSTR pszSpec)
{
	int iLen = wcslen(pszSpec), o, ret=0;
	WCHAR *p;

	while ((o=wcscspn(pszSpec,L"\\/:*?\"<>|"))<iLen)
	{
		switch (pszSpec[o])
		{
		case '/':
		case '\\':
			pszSpec[o]='-'; break;
		case '*':  pszSpec[o]='+'; break;
		case ':':  pszSpec[o]=';'; break;
		case '\"': pszSpec[o]='\''; break;
		case '<':  pszSpec[o]='('; break;
		case '>':  pszSpec[o]=')'; break;
		case '|':  pszSpec[o]='I'; break;
		default: pszSpec[o]='_'; break;
		}
		ret=1;
	}
	for (p=pszSpec+iLen-1; p>pszSpec && *p==' '; p--) *p=0;
	return ret;
}

//---------------------------------------------------------------------------

/* Creates a directory with the correct timestamp from a file name table entry.
 * If the directory already exists, it just sets the timestamp of the 
 * directory and all contained files.
 *
 * Params:
 *	pszDstDir	- Directory where the subdirectory will be created/updated
 *	idx			- Number of entry to be dumped. The directory name is prefixed
 *				  with this, as there can be multiple file names in the table
 *				  with the same name.
 *	fnt			- File name table entry.
 * Returns:
 *	-
 */
static void dumpFNTEntry(char *pszDstDir, int idx, FNTENTRY *fnt)
{
	SYSTEMTIME st={0};
	FILETIME ft;
	char outfile[MAX_PATH], *pfn=outfile;

	st.wYear = B2N_16(fnt->yy);
	st.wMonth = fnt->mm;
	st.wDay = fnt->dd;
	st.wHour = fnt->hh;
	st.wMinute = fnt->mi;
	st.wSecond = fnt->ss;
	pfn+=snprintf(outfile, sizeof(outfile), "%s\\[%03d]%s", pszDstDir, idx, fnt->progname);
	CreateDirectory(outfile, NULL);
	if (SystemTimeToFileTime(&st, &ft))
	{
		HANDLE hDir;
		WIN32_FIND_DATA find;
		HANDLE hFind;
		
		LocalFileTimeToFileTime(&ft, &ft);
		hDir = CreateFile(outfile, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
		if (hDir != INVALID_HANDLE_VALUE)
		{
			SetFileTime(hDir, &ft, &ft, &ft);
			CloseHandle(hDir);
		}
		strcpy (pfn++, "\\*.*");
		if ((hFind=FindFirstFile(outfile, &find)) != INVALID_HANDLE_VALUE)
		{
			do
			{
				strcpy(pfn, find.cFileName);
				hDir = CreateFile(outfile, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
				if (hDir != INVALID_HANDLE_VALUE)
				{
					SetFileTime(hDir, &ft, &ft, &ft);
					CloseHandle(hDir);
				}
			}
			while (FindNextFile(hFind, &find));
			FindClose(hFind);
		}
	}
}

//---------------------------------------------------------------------------

/* Prints the specified file name table entry to the console after converting
 * its charset to suit the console charset.
 * If pszDstDir is given, also creates a directory with the correct timestamp
 * from the file name table entry in this directory (see dumpFNTentry).
 *
 * Params:
 *	pszDstDir	- Directory where the subdirectory will be created/updated
 *				  Can be NULL, if directory creation is not needed.
 *	wszDstDir	- Wide char version of pszDstDir (only used if pszDstDir
 *				  is not NULL). This is needed for PathCleanupSpec() call
 *				  that ensures that the filename is correct for the target
 *				  file system.
 *  _PathClean..- This is the function pointer to the PathCleanupSpec function
 *				  of shell32.dll. It's not available on all versions of 
 *				  Windows, therefore it needs to be dynamically loaded. If
 *				  it is not available, the pointer can also be NULL.
 *	idx			- Number of entry to be dumped. The directory name is prefixed
 *				  with this, as there can be multiple file names in the table
 *				  with the same name.
 *	fnt			- File name table entry.
 * Returns:
 *	-
 */
static void printFNTEntry(char *pszDstDir, WCHAR *wszDstDir, fpPathCleanupSpec _PathCleanupSpec, int idx, FNTENTRY *fnt)
{
	WCHAR wszTmp[MAX_PATH+1];

	MultiByteToWideChar(CP_ACP,0,fnt->progname,-1,wszTmp,sizeof(wszTmp)/sizeof(wszTmp[0]));
	WideCharToMultiByte(CP_OEMCP,0,wszTmp,-1,fnt->progname,sizeof(fnt->progname),NULL,NULL);
	printf ("\r[%02d.%02d.%04d %02d:%02d:%02d] %s\n", 
		fnt->dd, fnt->mm, B2N_16(fnt->yy), fnt->hh, fnt->mi, fnt->ss, fnt->progname);
	if (pszDstDir) 
	{
		myPathCleanupSpec(wszTmp);
		if (_PathCleanupSpec) _PathCleanupSpec(wszDstDir,wszTmp);
		WideCharToMultiByte(CP_ACP,0,wszTmp,-1,fnt->progname,sizeof(fnt->progname),NULL,NULL);
		dumpFNTEntry(pszDstDir, idx, fnt);
	}
}

//---------------------------------------------------------------------------

/* Dumps the file at the given FAT entry from image file fp to output
 * file fpout.
 *
 * Params:
 *	fp			- Handle to the image file
 *	fpout		- Handle to the destination file where data will be dumped to.
 *	fat			- File allocation table entry.
 * Returns:
 *	TRUE on success, FALSE on failure.
 */
static BOOL dumpFile(HANDLE fp, HANDLE fpOut, FATENTRY *fat)
{
	LARGE_INTEGER offset;
	DWORD read, i, j, k, l, blocks;
	WORD cnk_blocks;
	BYTE buf[BLOCK_SIZE];
	ADDRESS dir[BLOCKDIR_ENTRIES];
	BLISTENTRY chunks[BLOCKDIR_ENTRIES];
	BOOL bRet=FALSE;

	blocks = B2N_32(fat->filesz);
	offset.QuadPart = B2N_32(fat->blockdir.block);
	offset.QuadPart *= BLOCK_SIZE;
	if (SetFilePointer(fp,offset.LowPart, &offset.HighPart,FILE_BEGIN) &&
		ReadFile(fp,&dir,sizeof(dir),&read,NULL) && read)
	{
		for (i=0, l=0; i<BLOCKDIR_ENTRIES; i++)
		{
			if (dir[i].block)
			{
				offset.QuadPart = B2N_32(dir[i].block);
				offset.QuadPart *= BLOCK_SIZE;
				if (SetFilePointer(fp,offset.LowPart, &offset.HighPart,FILE_BEGIN) &&
					ReadFile(fp,&chunks,sizeof(chunks),&read,NULL) && read)
				{
					for (j=0; j<BLIST_ENTRIES; j++)
					{
						/*if (j%10==0) */printf ("\rDumping %d/%d blocks...%d%%", l, blocks, 
							(int)(((double)l/(double)blocks)*100));
						if (chunks[j].block)
						{
							offset.QuadPart = B2N_32(chunks[j].block);
							offset.QuadPart *= BLOCK_SIZE;
							cnk_blocks = B2N_16(chunks[j].blocks)&0x7FFF;
							if (SetFilePointer(fp,offset.LowPart, &offset.HighPart,FILE_BEGIN))
							{
								for (k=0; k<cnk_blocks; k++)
								{
									if (ReadFile(fp,buf,sizeof(buf),&read,NULL) && read)
									{
										if (*((unsigned long*)buf)!=VOB_MAGIC)
											fprintf (stderr, "\rWarning: Block #%d (@%X%08X) has no MPEG header!\n",
												k, offset.HighPart, offset.LowPart);
										if (!WriteFile(fpOut,buf,sizeof(buf),&read,NULL))
										{
											fprintf(stderr, "\r%s: Cannot write block #%d to output file: ", "Blocklist", k);
											ShowError();
										}
									}
									else
									{
										fprintf(stderr, "\r%s: Cannot read block #%d: ", "Blocklist", k);
										ShowError();
									}
								}
							}
							else
							{
								fprintf(stderr, "\r%s: Cannot seek to offset %X%08X: ", "Blocklist", offset.HighPart, offset.LowPart);
								ShowError();
							}
							l+=cnk_blocks;
						}
					}
				}
				else
				{
					fprintf(stderr, "\r%s: Cannot seek to offset %X%08X: ", "Block directory", offset.HighPart, offset.LowPart);
					ShowError();
				}
			}
		}
		bRet = l==blocks;
		printf ("\rDumping %d/%d blocks...%s\n", l, blocks, l<blocks?"Incomplete":"Done");
	}
	else
	{
		fprintf(stderr, "Cannot seek to offset %X%08X: ", offset.HighPart, offset.LowPart);
		ShowError();
	}
	return bRet;
}


/****************************************************************************
 *                                   E I P                                  *
 ****************************************************************************/
int main(int argc, char **argv)
{
	int i;
	DWORD dwXthresh=SCR_THRESH_X, dwMthresh=SCR_THRESH_M, dwOffset=0, dwStart=0;

	printf ("Simple Pioneer DVR-633H recovery\n(c) by leecher@dose.0wnz.at 2014\n\n");
	for (i=1; i<argc; i++)
	{
		if (argv[i][0]!='-')
		{
			fprintf(stderr, "You need to specify an action!\n\n");
			break;
		}
		switch (argv[i][1])
		{
		case 'h':
		case '?':
			return usage(argv[0]);
		case 's':
		case 'o':
		case 'c':
			if (argc<=i+1)
			{
				fprintf(stderr, "%s needs a value!\n", argv[i]);
				return EXIT_FAILURE;
			}
			switch (argv[i][1])
			{
			case 's':
				switch (argv[i][2])
				{
				case 'x': dwXthresh = strtoul(argv[++i], NULL, 0); break;
				case 'm': dwMthresh = strtoul(argv[++i], NULL, 0); break;
				default:
					fprintf(stderr, "-s: You need to set -sx or -sm!\n");
					return EXIT_FAILURE;
				}
				break;
			case 'o':
				dwOffset = strtoul(argv[++i], NULL, 0);
				break;
			case 'c':
				dwStart = strtoul(argv[++i], NULL, 0);
				break;
			}
			break;
		case 'p':
		case 'x':
		case 'd':
			if (argc<=i+2)
			{
				fprintf(stderr, "%s needs source file and destination directory!\n", argv[i]);
				return EXIT_FAILURE;
			}
			switch (argv[i][1])
			{
			case 'd':
				return dir(argv[i+1], argv[i+2], MAX_DIRSRCH, MODE_EXTRACT)?EXIT_SUCCESS:EXIT_FAILURE;
			case 'L':
				return dir(argv[i+1], argv[i+2], MAX_DIRSRCH, MODE_HEUR)?EXIT_SUCCESS:EXIT_FAILURE;
			}
			if (!extract(argv[i+1], argv[i+2], dwOffset, dwStart, dwXthresh)) return EXIT_FAILURE;
			if (argv[i++][1]!='p') return EXIT_SUCCESS;
		case 'm':
			if (argc<=i+1)
			{
				fprintf(stderr, "%s needs destination directory!\n", argv[i]);
				return EXIT_FAILURE;
			}
			return merge(argv[++i], dwMthresh)?EXIT_SUCCESS:EXIT_FAILURE;
		case 'l':
			if (argc<=i+1)
			{
				fprintf(stderr, "%s needs source file!\n", argv[i]);
				return EXIT_FAILURE;
			}
			if (!dir(argv[++i], NULL, MAX_DIRSRCH, MODE_USEFS))
				return dir(argv[++i], NULL, MAX_DIRSRCH, MODE_HEUR)?EXIT_SUCCESS:EXIT_FAILURE;
		default:
			fprintf(stderr, "Unknown commandline option: %s\n\n", argv[i]);
			break;
		}
	}
	return usage(argv[0]);
}
