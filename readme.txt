POCSAG via si4432-based ISM modules
-----------------------------------


This application-set is designed to send 512 bps POCSAG-messages
using si4432-based ISM transceivers.

It uses a linux device to generate the POCSAG (alphanumeric) messages
and a arduino to interface to the si4432 chipset.


The package uses two applications:
- sendpoctxt.c:
C application that creates the POCSAG-message and sends the
message (plus callsign and configuration data) over serial port
to the arduino


- rf22_pocsag.ino:
Arduino application that receives POCSAG-message from the serial
port and transmits it via the si4432 module.

rf22_pocsag.ino uses the "arduino rf22"-library found at
http://www.airspayce.com/mikem/arduino/RF22/



As POCSAG-message natively do not contain an identification of the
transmitting station, a small FSK-based and a FM-modulated CW
identification can be added to the transmission.



Compile / install:
gcc -Wall -o sendpoctxt sendpoctxt.c


connect arduino to serial port of linux board (/dev/ttyAMA0)

./sendpoctxt MYCALL <address> <address-source> "POCSAG is good for you"




These programs are free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the
License, or (at your option) any later version.



Version 0.1.0 (21040330) initial version
