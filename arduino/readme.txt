POCSAG via si4432-based ISM modules
-----------------------------------


This application is designed to send 512 bps POCSAG-messages
using si4432-based ISM transceivers.

It uses an arduino to create POCSAG-message and to interface
with the si4432 based ISM-tranceiver for broadcasting the message.

// this program uses the arduino rf22 library
// http://www.airspayce.com/mikem/arduino/RF22/


- rf22_pocsag_arduino.ino:
Arduino application that receives the text message to be send
from the serial port, creates the pocsag message and transmits
it via the si4432 module.

rf22_pocsag.ino uses the "arduino rf22"-library found at
http://www.airspayce.com/mikem/arduino/RF22/



As POCSAG-message natively do not contain an identification of the
transmitting station, an FM-modulated CW identification is added
at the end of the transmission.



The application has a very basic CLI, used to demo the application:

Format:
P <address> <source> <callsign> <repeat> <message>
F <freqmhz> <freq100Hz>

The default frequency is 439.9875 Mhz.
(allocated to ham-radio pocsag in IARU-R1).

Press "escape" to reset the CLI.



Note that a lot of the si4432-based RF modules do not contain a very
stable crystal. Usually it is needed to add or subtract some offset to
the frequency-setting to compensate for this error.
In my case, to transmit on 439.9875, I need to configure the device
to 439.9575 (300 Khz below the correct frequency)




Wiring Functions:
- setup(): setup serial port + the si4432 device
- loop(): CLI + sending message

Main functions:
- create_pocsag: create pocsag message based on text-message provided
- sendcwid: send FSK-based id

Support functions:
- replaceline
- flip7charbitorder
- createcrc


The source-code contains a lot of documentation on the format of
alpha-numeric pocsag messages. Check out the "create_pocsag" function.




This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the
License, or (at your option) any later version.


Repository:
https://github.com/on1arf/rf22_pocsag


Version 0.1.1 (21040501) initial version
Kristoff Bonne (ON1ARF)
