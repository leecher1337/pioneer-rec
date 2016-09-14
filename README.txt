The problem
===========
So there you are with your Pioneer DVR633H-S, DVR545H, DVR-550H or Sony HXD995
showing you an HDD Error and refusing to boot...
This usually indicates that the harddrive in the recorder went bad.
But don't panic, there may be hope!

Create a HDD image
==================
First you have to remove the harddisk from the DVR device. There is a nice
guide for this on the PioneerFAQ website:

http://www.pioneerfaq.info/english/dvr630.php

After removing the HDD, you can mount it to your PC, i.e. with an USB
Adapter or (preferably) directly to the IDE interface.

If the PC recognizes the drive, you may have luck and it is only partly 
damaged and data can be rescued. i.e. on my HDD, Sector 0 and 1 were
damaged, so it took ages until it was accepted by the operating system,
but it worked.

So the next thing to do is to dump the contens of the disk to an image
file so you need to have at least the size of the HDD
(256GB) available on one of your harddisks to make an image. 
You may need up to the same size for the extracted movie data then, 
but this can also be on another drive then. 
There are many tools available for dumping a harddisk to an image file,
i.e. Datarescue's drdd:

http://www.datarescue.com/photorescue/v3/drdd.htm

However due to the way how windows handles bad drives, it's usually a pain
dumping them on Windows, i.e. if the drive detaches itself from the bus 
during read und is connected via a USB-Adapter, most dumper programs including
drdd just continue dumping and writing lots of faulty sectors even if the 
sectors would be readable after the drive reattached itself to the bus after 
reset.
Therefore I recommend using a linux BootCD/system and dd_rescue like also
mentioned in the guide of pioneerfaq.

After creating the image, you are set to use my utility to recover your data.

Recovering movies the nice & easy way
=====================================
Now the strategy on how to recover your remaining movie data largely depends
on the way your data on the HDD has been damaged.
If the file allocation structures are intact, you have a good chance for 
a painless recovery using this tool.
I assume that the name of the image you created in the previous step is 
image.dd

You need to have an empty destination directory where the movies will be
recovered to. Assuming your destination directory is f:\dump just issue:

pioneer_rec -d image.dd f:\dump

This will create directories with the "program" names of the dumped movies
and put the movie files belongig to the "program" into it. It also time-
stamps the directories according to the recording date.

If there are some errors about not finding proper MPEG-headers, don't worry,
these may be bad sectors, but the extracted movies may still be playable.

If the program outputs severe errors and is unable to parse the directory
information, you may want to adjust the -O parameter for the directory 
offset by looking at the dumped image file. For me this table started at 
offset 0x2004000 on the disk, but it may be different for different VCR 
models or harddisks, I don't know. Generally, look for the first table 
after offset 0x2000000 in the image, maybe you find it in there.


Extracting movies from severely damaged HDDs
============================================
If you are not aware how to do this or are unable to find the correct offset
or - more likely - your image is just too damaged to hold a valid filesystem
structure, you may want to try a different approach. This one was described
by Stefan Haliner in the avsforum. He basically made a python script that
extracts all consecutive mpeg-blocks from an image and assembles them 
together. You can fetch his script here:

https://github.com/haliner/dvr-recover

pioneer_rec can basically do the same, but doesn't have such a sophisticated
merging approach using a database with chunks. However you may still want
to give it a try, at least you don't need to install python for this and
there also is some nice feature that may help you with sorting your movies
by date in case the directory info is still readable.

If you extracted your movie chunks with dvr-recover, you can go forward to
step 3, otherwise here is how to extract it with pioneer_rec:

1) First, extract the movie chunks with the -x command to an empty directory,
   i.e. if your chunk directory is f:\dump do:

   pioneer_rec -x image.dd f:\dump

   The program assumes that your image is valid and mpeg-frames always start 
   at a block (1 block = 2KB) boundary.
   In case you are extracting from multiple part-images and they are not
   block-aligned, you may need to adjust the initial offset with the -o 
   parameter.
   pioneer_rec uses a very primitive algorithm to split video chunks:
   It is checking for the MPEG Pack header (00 00 01 BA) which also contains
   a SCR (System clock reference) value.
   It uses the high part of this value (the next following 4 bytes) and checks
   if the next chunk on the disk has a SCR that is lower than the threshold
   (curretly this is 0x600 which seems to be a good value). If yes, then the
   next chunk still contains to the current movie, otherwise it may be a chunk 
   belonging to a different one and extraction continues in next file.
   The extracted chunks are numbered sequentially which is important for 
   merging in the next step as it indicates the order of them on disk.

2) After extracting all the chunks found, you may need to merge them to get 
   full movies. As there are multiple possibilities for this, you may want 
   to copy the extracted chunks to a backup location so that you can restore
   them via a simple copy if merging just does too much.
   pioneer_rec uses a very primitive algorithm to merge together video chunks:
   It just checks the MPEG-Header mentioned above, if the the SCR is between 
   SCR+4 and SCR+5.
   It loops through the video chunks extracted in the previous step and checks
   their ending. Then it iterates over the rest of the chunks and checks their
   beginning. If the beginning is within the mentioned range, the found chunk
   is appended to the current chunk and deleted and the search starts over 
   again. This is needed as due to fragmentation, the chunks can be 
   distributed everywhere on disk.
   You can adjust the range to search via the -sm parameter, however the 
   default of 5 should be OK. But don't expect too much accuracy, as 
   surprisingly there are quite a lot of chunks that match, even though they 
   belong to a different movie! So you may still need to cut&merge manually 
   afterwards.
   To initiate merging, use the -m option on your chunk directory:

   pioneer_rec -m f:\dump

   Afterwards the number of chunks is reduced.

3) After you have the merged video chunks either produced by pioneer_rec or by
   dvr-recover, you need to check all movies to ensure they are not garbled 
   through merging.
   If you need to cut and merge them, I suggest using mpg2cut2:

   http://rocketjet4.tripod.com/Mpg2Cut2.htm

   Skip around with < and >
   if you reached the point of break, go back with < and slowly move forward
   frame by frame with /
   Sometimes this doesn't work properly, then use , to skip forward one frame
   Then split with |
   Then Save Parts (SHIFT+F4) and select Split @Clip.

4) After you recreated most of your movies and you have luck that the file 
   names in your image are still OK, you can use pioneer_rec to create a 
   directory structure for the movies for you. It automatically adjusts the 
   timstamp of these directories to the timestamp of the recording from the 
   VCR. If there are files within the directories, they are timestamped
   accordingly too.
   You just need an output directory, i.e. f:\movies and then issue:

   pioneer_rec -L f:\movies

   Then you can sort all your chunks into the appropriate directories. 
   Afterwards run the command again as by sorting, timestamps have changed 
   again to reset them and the date of the movie files to the original dates.

Good luck with recovery! If you have problems, especially with the automated
recovery (-d option), feel free to contact me so that you can send me the 
header part of your disk image where I can have a look, that may help me 
improving my extraction routines.

Installing a replacement Harddisk into your DVR recorder
========================================================
This topic is already covered in the PioneerFAQ article mentioned above, but
it says that you are requiring a DVD Recorder Data Disc (GGV1239) and a
Pioneer service remote control model number GGF1381/GGF1595.
As for the data disc, just ask the nice author of the pioneerFAQ and make
a donation and you will get it from him.
A more severe problem is to obtain the Pioneer service remote. First,
you most likely don't want to waste money just for swapping the harddisk
and second, it's not really easy to get it.
So I was looking for ways to proceed without it and I found a WinLIRC file
for this remote: pioservrmtGGF1381.cf
It can be downloaded in the package winlirc-0.6.5-ggf1381.rar from here: 
http://www.dvdboard.de/forum/showthread.php?121319-Pioneer-service-disc-GGV-%28DVD%29

However I was too lazy to solder a sending circuit for use with WinLIRC,
but I found out that the IrDA Port in my Thinkpad Notebook can also be used
for it.
So I tried this: http://slydiman.me/sce/plugins/ir210.htm
Thge correct Settings fpr WinLIRC can be found here:
http://www.videohelp.com/dvdhacks/pioneer-dvr-440h/6947#25993
Reciever type: RX
Virtual Pulse: 300
Speed: 115200
Transmitter Settings: Unselect hardware carrier and select TX 

I got the IR sender to transmit data (I checked this with a IR receiver), but
I was unable to emit a signal that was recognized by the Pioneer DVR.
Fortunately, I found a thread where a user provided a code file for the 
DOS-program winsamp (http://www.veg.nildram.co.uk/remote.htm) for the 
service remote:
https://www.avforums.com/threads/succesful-upgrade-of-hdd-on-rdr-hxd970.675639/page-3#post-6158149
In case it gets lost, I archived the files, just write me an e-mail.
I tried to use the program in Windows, it emitted signals via IRDA, but they
also weren't recognized by the DVR. I suppose this is due to timing issues in 
a multitasking environment like Windows. So I really booted up DOS from a
floppy and tried it there and suprise: IT REALLY WORKS!
NOTE: The STOP key mentioned in the PDF corresponds to the "OpenClose" key.

So I was able to swap the Harddisk successfully just using my Thinkpad.
If you also own a Thinkpad, don't forget to assign the IrDA port to a 
COM-Port (usually COM2 at Address 3F8h) in BIOS, this is normally not enabled.
Enter BIOS settings with F1. Afterwards you can change it back again.


Technical details
=================
I gathered the structure of the on-disk filesystem through looking at the
dump of my partly damaged drive with a hex-editor.
Knowing that the block size is 2048 bytes, I found some block pointers that
revealed the most important key points of the file allocation structure 
when comparing them to the movie descriptions.
The layout of (at least my) harddisk seems to be:

0x00000000 - Boot block? My first 2 sectors were unfortunately damaged
0x00000800 - Start of GUIDE+ Firmware
0x00000838 - DWORD of block number where File name table directory starts
0x003E8800 - End   of GUIDE+ Firmware
0x02004000 - File name table directory
0x02004200 - Table with FNT offsets to MRU (must recently used) files? 
0x02004A00 - Some bitmap?
0x02004A80 - File allocation table directory
0x020062F0 - Some other bitmap?
0x02006AE8 - File order table directory
0x020062F0 - Another bitmap?
...

Now let's see how all these tables are related to each other:
Please note that the names of the tables were made up by me!


        @ 0x2004000
+----------------------+
| File name table dir  |
+----------------------+  FNT
| Block # of Table 1   +->+--------------------+
| Block # of Table 2   |  | File name table    | +--------------------------+
| ...                  |  +--------------------+ | File name table entry    |
| Block # of Table 64  |  | File name entry 1  | +--------------------------+
+----------------------+  | File name entry 2  |/| Recording date           |
                          | ...                |\| File Order table offset  ++
        @ 0x2006AE8       | File name entry 21 | | Number of files of progr.||
+----------------------+  +--------------------+ | Recording source channel ||
| File order table dir |                         | Program name             ||
+----------------------+  FOT                    | ...                      ||
| Block # of Table 1   +->+--------------------+ +--------------------------+|
| Block # of Table 2   |  | File order table   |<----------------------------+
| ...                  |  +--------------------+
| Block # of Table 256 |  | File order entry 1 | +-------------------------+
+----------------------+  | File order entry 2 |/| File order table entry  |<+ 
                          | ...                |\+-------------------------+ |
        @ 0x2004A80       | File order en. 128 |++ FAT offset of file      | |
+----------------------+  +--------------------+|| FOT offset of next file +-+
| File alloc.tbl dir   |                        || ...                     |
+----------------------+  FAT                   |+-------------------------+
| Block # of Table 1   |->+-------------------+ |
| Block # of Table 2   |  | File allocation t |<+
| ...                  |  +-------------------+  +-------------------------+
| Block # of Table 256 |  | FAT entry 1       | /| FAT entry               |
+----------------------+  | FAT entry 2       |/ +-------------------------+
                          | ...               |\ | Size of file in blocks  |
                          | FAT entry 96      | \| Block # of Blockdir     ++
                          +-------------------+  | ...                     ||
+----------------------+                         +-------------------------+|
| Block dir of a file  |<---------------------------------------------------+
+----------------------+  +---------------------+
| Block # of Table 1   |->| Block list    BLIST | +------------------------+
| Block # of Table 2   |  +---------------------+ | Block list entry       |
| ...                  |  | Blocklist entry 1   |/+------------------------+
| Block # of Table 256 |  | Blocklist entry 2   |\| Block # of data        ++
+----------------------+  | ...                 | | # of blocks @ data     ||
                          | Blocklist entry 256 | | ...                    ||
                          +---------------------+ +------------------------+|
                                                                            |
              +--------------------------------------------------+          |
              |                      D A T A                     |<---------+
              +--------------------------------------------------+


Please note that all numbers are BIG ENDIAN!
First there is the file name table directory. This contains a list of blocks
that each point to a filename table with 21 entries. 
Such addresses are 8 bytes in size (see ADDRESS struct): The first DWORD
is the block number, the second DWORD is unknown to me.
The structure of a file name table entry is documented in the FNTENTRY struct.
A file name (A better word for it would be "Program name table") table
entry also contains the number of files the mentioned program consists
of, because there can be multiple movie files assigned to it!
So such an entry can be compared to a directory on a normal PC filesystem.
The entry also contains a file order table offset.
This 1-based offset points to an entry in the file order table.
As this is also a directory pointing to 128 entries each, you have to 
calculate the offset by 128, i.e. if the Offset is 129, it's the first
entry in the second table ((129-1)/128=1, (129-1)%128=0).

A file order table entry is documented in the struct FOTENTRY.
As previously mentioned there can be more files assigned to a filename.
These files are managed in a list of offsets into the file order table
and there is a 1-based entry pointing at the next file order table entry.
Same calulcation formula as above applies. If the offset is 0, it's the
end of the list and you have reached the last file.
Each entry contains an offset into the File allocation table that then points
to the real file:

A file allocation table entry is documented in the struct FATENTRY.
It contains the size of the file in blocks and an address of a block
directory that points to a block directory that in turn points to the
block lists that point to the blocks that finally make up the file.
Each block list entry contains a pointer to the acutal block on the disk
and the number of consecutive blocks at that block that are part of the
file. The highest bit of the block count needs to be masked off.

Porting
=======
If you are interested in a Linux port and are unable to do it yourself, feel
free to contact me, I can port it to Linux too if needed.

Contact
=======
I'd love to get some feedback on this if it helped you!
Contact me at leecher@dose.0wnz.at
