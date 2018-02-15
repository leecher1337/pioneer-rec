So there you are with your Pioneer DVR633H-S, DVR545H, DVR-550H, DVR-520H or Sony HXD995 showing you an HDD Error and 
refusing to boot...
This usually indicates that the harddrive in the recorder went bad.
But don't panic, there may be hope!

Unlike other utilities for this task, this program is capable of reconstructing the movies from the harddrive by parsing the filesystem structure of the Pioneer DVR instead of just dumping MPEG-chunks!

For further information on how to use it and for technical details about the filesystem structure, please refer to the [README](https://github.com/leecher1337/pioneer-rec/blob/master/README.txt) file. 
For the impatient ones, assuming you have a disk image image.dd and a recovery destination directory f:\dump, just do:

`pioneer_rec -d c:\image.dd f:\dump`

Binary: [pioneer_rec.zip](http://dose.0wnz.at/scripts/cpp/pioneer_rec.zip)

Discussion: [Blog post](http://hardwarefetish.com/584-pioneer-dvr-recorder-harddisk-recovery)

Here is a compatibility matrix of the recorders that were reported to work with the respective version of the utility:

Header     | Pioneer                       | Sony
---------- | ----------------------------- | -----------------
dvr520h.h  | DVR-520H                      | 
dvr545h.h  | DVR-545H                      |
dvr550h.h  | DVR-550H                      | HXDx90, HXD995, HXD1070
dvr633h.h  | DVR-530H, DVR-633H, DMR-XW300 |


For Panasonic DVR recorders, you can have a look at my [panasonic-rec](https://github.com/leecher1337/panasonic-rec) project.
