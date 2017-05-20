ts2mpa
======
By Nicholas Humfrey <njh@aelius.com>

ts2mpa is a simple tool to extract MPEG Audio from a MPEG-2 Transport Stream.

Usage:

    ts2mpa [options] <infile> <outfile>
      -h             Help - this message.
      -q             Quiet - don't print messages to stderr.
      -p <pid>       Choose a specific transport stream PID.
      -s <streamid>  Choose a specific PES stream ID.



Examples
--------

The following examples are based on DVB-T in London, tuned to 
BBC Radio coming from the the Crystal Palace transmitter:

Record BBC Radio 4 to disk:

    dvbstream -o -f 529833330 439 | ts2mpa - recording.mp2

Play BBC Radio 1 through soundcard:

    dvbstream -o -f 529833330 436 | ts2mpa -q - - | mpg123 -


License
-------

This program is free software: you can redistribute it and/or modify
it under the terms of the [GNU General Public License] as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
