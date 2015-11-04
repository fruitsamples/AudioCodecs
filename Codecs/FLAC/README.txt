FLAC Readme

23 August 2007

Before building the FLAC project one must download the most recent sources (as of this writing version 1.2.0) from:

http://flac.sourceforge.net/download.html

Go to "Source code" heading and click on the "FLAC full source code" link.

Once the sources are downloaded, copy the include and src/libFLAC directories to AudioCodecs/Codec/FLAC/.

At ths point, the only thing that needs to be done is to define the version for format.c. The easiest way to do this is to add these lines to format.c before the FLAC__VERSION_STRING is defined:

#ifndef VERSION
#define VERSION "1.2.0"
#endif

At this point the project should build. Take the resulting FLAC.component and put it in /Library/Components. You now have a working FLAC encoder and decoder on the system.
