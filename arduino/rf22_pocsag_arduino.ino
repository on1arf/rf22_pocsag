// rf22_pocsag_arduino

// receive POCSAG-message + callsign + configuration data from serial port
// send message on RF (70 cm ham-band) using si4432-based "RF22" ISM modules


// this program uses the arduino rf22 library
// http://www.airspayce.com/mikem/arduino/RF22/

// (c) 2014 Kristoff Bonne (ON1ARF)

/*
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License as
 *  published by the Free Software Foundation; either version 2 of the
 *  License, or (at your option) any later version.
 *
 *  This application is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

// Version 0.1.0 (20140330) Initial release
// Version 0.1.1 (20140501) pocsag message creation now done on arduino


// increase maximum packet site
#define RF22_MAX_MESSAGE_LEN 255

#define CWID_SHORT 33 // 106 ms @ 1200 bps
#define CWID_LONG 90 // 320ms @ 1200 bps

#include <SPI.h>
#include <RF22.h>


// global data

// RF22 class
RF22 rf22;
RF22::ModemConfig MC;

uint8_t cwidmsg[CWID_LONG];

// frequency;
float freq;




// structure for pocsag message (containing two batches)
struct pocsagmsg {
  unsigned char sync[72];

  // batch 1
  unsigned char synccw1[4];
  int32_t batch1[16];

  // batch 2
  unsigned char synccw2[4];
  int32_t batch2[16];
}; // end 


// structure to convert int32 to architecture  / endian independant 4-char array
struct int32_4char {
  union {
    int32_t i;
    unsigned char c[4];
  }; 
}; 



// data-structure for cwid
struct t_mtab {
  char c;
  int pat;
};

// table morse chars, read from left to right, ignore first bit (used to determine size)
struct t_mtab morsetab[] = {
  	{'.', 106},
	{',', 115},
	{'?', 76},
	{'/', 41},
	{'A', 6},
	{'B', 17},
	{'C', 21},
	{'D', 9},
	{'E', 2},
	{'F', 20},
	{'G', 11},
	{'H', 16},
	{'I', 4},
	{'J', 30},
	{'K', 13},
	{'L', 18},
	{'M', 7},
	{'N', 5},
	{'O', 15},
	{'P', 22},
	{'Q', 27},
	{'R', 10},
	{'S', 8},
	{'T', 3},
	{'U', 12},
	{'V', 24},
	{'W', 14},
	{'X', 25},
	{'Y', 29},
	{'Z', 19},
	{'1', 62},
	{'2', 60},
	{'3', 56},
	{'4', 48},
	{'5', 32},
	{'6', 33},
	{'7', 35},
	{'8', 39},
	{'9', 47},
	{'0', 63},
   {' ', 0}
} ;


// table to convert size to n-times 1 bit mask
const unsigned char size2mask[7] = {0x01, 0x03, 0x07, 0x0f, 0x1f, 0x3f, 0x7f};

// functions defined below
void sendcwid(char * idtxt);
uint32_t createcrc(uint32_t);


void setup() 
{
  
  Serial.begin(9600);
  if (rf22.init()) {
    Serial.println("RF22 init OK");
  } else {
    Serial.println("RF22 init FAILED");
  }; // end if


// initialise at 439.9875
// you will probably need to change this to correct for crystal error

freq=439.9875;
rf22.setFrequency(freq, 0.00);


  // configure radio-module for 512 baud, FSK, 4.5 Khz fd
  MC.reg_6e=0x04; MC.reg_6f=0x32; MC.reg_70=0x2c; MC.reg_58=0x80;
  MC.reg_72=0x04; MC.reg_71=0x22;

  MC.reg_1c=0x2B; MC.reg_20=0xa1; MC.reg_21=0xE0; MC.reg_22=0x10;
  MC.reg_23=0xc7; MC.reg_24=0x00; MC.reg_25=0x09; MC.reg_1f=0x03;
  MC.reg_69=0x60;
  
  rf22.setModemRegisters(&MC); 


  // 100 mW TX power
  rf22.setTxPower(RF22_TXPOW_20DBM);
  


  // the rest of the two batches are read from the serial port
  // see loop function below

  // CW: dummy packet containing 0b01010101
  memset(cwidmsg,0x55,CWID_LONG);

}

void loop() {
//vars
struct pocsagmsg msg;
int size;
int rc;

int state;

// bell = ascii 0x07
char bell=0x07;

// data
long int address;
int addresssource;
int repeat;
char call[8]; // callsign
int callsize;
char textmsg[42]; // to store text message;
int msgsize;

int freqsize;
int freq1; // freq MHZ part (3 digits)
int freq2; // freq. 100Hz part (4 digits)

// read input:
// format: "P <address> <source> <callsign> <repeat> <message>"
Serial.println("POCSAG text-message tool v0.1");
Serial.println("https://github.com/on1arf/rf22_pocsag");
Serial.println("");
Serial.println("Format:");
Serial.println("P <address> <source> <callsign> <repeat> <message>");
Serial.println("F <freqmhz> <freq100Hz>");


// init var
state=0;
address=0;
addresssource=0;
callsize=0;
msgsize=0;

freqsize=0;

while (state >= 0) {
char c;
	// loop until we get a character from serial input
	while (!Serial.available()) {
	}; // end while

	c=Serial.read();

        // break out on ESC
        if (c == 0x1b) {
          state=-999;
          break;
        }; // end if

	// state machine

	if (state == 0) {
	// state 0: wait for command
        // P = PAGE
        // F = FREQUENCY
        
		if ((c == 'p') || (c == 'P')) {
			// ok, got our "p" -> go to state 1
			state=1;
			// echo back char
			Serial.write(c);
                } else if ((c == 'f') || (c == 'F')) {
			// ok, got our "f" -> go to state 1
			state=10;
			// echo back char
			Serial.write(c);
		} else {
			// error: echo "bell"
			Serial.write(bell);
		}; // end else - if

		// get next char
		continue;
	}; // end state 0

	// state 1: space (" ") or first digit of address ("0" to "9")
	if (state == 1) {
		if (c == ' ') {
			// space -> go to state 2 and get next char
			state=2;

			// echo back char
			Serial.write(c);

			// get next char
			continue;
		} else if ((c >= '0') && (c <= '9')) {
			// digit -> first digit of address. Go to state 2 and process
			state=2;

			// continue to state 2 without fetching next char
		} else {
			// error: echo "bell"
			Serial.write(bell);

			// get next char
			continue;
		}; // end else - if
	};  // end state 1

	// state 2: address ("0" to "9")
	if (state == 2) {
		if ((c >= '0') && (c <= '9')) {
			long int newaddress;

			newaddress = address * 10 + (c - '0');

			if (newaddress <= 0x1FFFFF) {
				// valid address
				address=newaddress;

				Serial.write(c);
			} else {
				// address to high. Send "beep"
				Serial.write(bell);
			}; // end if

		} else if (c == ' ') {
			// received space, go to next field (address source)
			Serial.write(c);
			state=3;
		} else {
			// error: echo "bell"
			Serial.write(bell);
		}; // end else - elsif - if

		// get next char
		continue;
	}; // end state 2

	// state 3: address source: one single digit from 0 to 3
	if (state == 3) {
		if ((c >= '0') && (c <= '3')) {
			addresssource= c - '0';
			Serial.write(c);

			state=4;
		} else {
			// invalid: sound bell
			Serial.write(bell);
		}; // end if

		// get next char
		continue;
	}; // end state 3


	// state 4: space between source and call
	if (state == 4) {
		if (c == ' ') {
			Serial.write(c);

			state=5;
		} else {
			// invalid: sound bell
			Serial.write(bell);
		}; // end if

		// get next char
		continue;
	}; // end state 4

	// state 5: callsign, minimal 4 chars, maximum 7 chars
	if (state == 5) {
		// make uppercase if possible
		if ( (c >= 'a') && (c <= 'z')) {
			// capical letters are 0x20 below small letters in ASCII table
			c -= 0x20; 
		}; // end if

		// characters allowed are [A-Z] and [0-9]
		if ( ( (c >= 'A') && (c <= 'Z')) ||
				((c >= '0') && (c <= '9')) ) {
			// accept if not more then 7 chars
			if (callsize < 7) {
				// accept
				call[callsize]=c;
				Serial.write(c);
				callsize++;
			} else {
				// to long, error
				Serial.write(bell);
			}; // end else - if
		} else if (c == ' ' ) {
			// space, accept space if we have at least 4 chars
			if (callsize >= 4) {
				Serial.write(c);

				// terminate string with "0x00"
				call[callsize]=0x00;
				
				// go to next state
				state=6;
			} else {
				Serial.write(bell);
			}; // end else - if
		} else {
			// invalid char
			Serial.write(bell);
		}; // end else - elsif - if

		// get next char
		continue;
	}; // end state 5


	// state 6: repeat: 1-digit value between 0 and 9
	if (state == 6) {
		if ((c >= '0') && (c <= '9')) {
			Serial.write(c);
			repeat=c - '0';

			// move to next state
			state=7;
		} else {
			Serial.write(bell);
		}; // end if

		// get next char
		continue;
	}; // end state 6


	// state 7: space between repeat and message
	if (state == 7) {
		if (c == ' ') {
			Serial.write(c);

			// move to next state
			state=8;
		} else {
			// invalid char
			Serial.write(bell);
		}; // end else - if

		// get next char
		continue;
	}; // end state 7


	// state 8: message, up to 40 chars, terminate with cr (0x0d) or lf (0x0a)
	if (state == 8) {
		// accepted is everything between space (ascii 0x20) and ~ (ascii 0x7e)
		if ((c >= 0x20) && (c <= 0x7e)) {
			// accept up to 40 chars
			if (msgsize < 40) {
				Serial.write(c);

				textmsg[msgsize]=c;
				msgsize++;
			} else {
				// to long
				Serial.write(bell);
			}; // end else - if
			
		} else if ((c == 0x0a) || (c == 0x0d)) {
			// done
   
			Serial.println("");
			
			// add terminating NULL
			textmsg[msgsize]=0x00;

			// break out of loop
			state=-1;
			break;

		} else {
			// invalid char
			Serial.write(bell);
		}; // end else - elsif - if

		// get next char
		continue;
	}; // end state 8;




        // PART 2: frequency

	// state 1: space (" ") or first digit of address ("0" to "9")
	if (state == 10) {
		if (c == ' ') {
			// space -> go to state 11 and get next char
			state=11;

			// echo back char
			Serial.write(c);

			// get next char
			continue;
		} else if ((c >= '0') && (c <= '9')) {
			// digit -> first digit of address. Go to state 2 and process
			state=11;

                        // init freqsize;
                        freqsize=0;
                        freq1=0;

			// continue to state 2 without fetching next char
		} else {
			// error: echo "bell"
			Serial.write(bell);

			// get next char
			continue;
		}; // end else - if
	};  // end state 1



	// state 11: freq. Mhz part: 3 digits needed
	if (state == 11) {
  	  if ((c >= '0') && (c <= '9')) {
            if (freqsize < 3) {
               freq1 *= 10;
               freq1 += (c - '0');
               
               freqsize++;
               Serial.write(c);
               
               // go to state 12 (wait for space) if 3 digits received
               if (freqsize == 3) {
                  state=12;
               }; // end if
            } else {
               // too large: error
               Serial.write(bell);
            }; // end else - if
          } else {
           // unknown char: error
            Serial.write(bell);
          }; // end else - if
          
         // get next char
         continue;
          
         }; // end state 11
         

        // state 12: space between freq part 1 and freq part 2            
        if (state == 12) {
          if (c == ' ') {
            // space received, go to state 13
                state = 13;
                Serial.write(c);
                
                // init freq2; + freqsize;
                freq2=0;
                freqsize=0;                
          } else {
             // unknown char
             Serial.write(bell);
          }; // end else - if

         // get next char
         continue;

      }; // end state 12;
               
      // state 13: part 2 of freq. (100 Hz part). 4 digits needed
      if (state == 13) {
  	  if ((c >= '0') && (c <= '9')) {
            if (freqsize < 4) {
               freq2 *= 10;
               freq2 += (c - '0');
               
               freqsize++;
               Serial.write(c);
            } else {
               // too large: error
               Serial.write(bell);
            }; // end else - if
            
            // get next char
            continue;
            
          } else if ((c == 0x0a) || (c == 0x0d)) {
              if (freqsize == 4) {
                // 4 digits received, go to next state
                state = -2;
                Serial.println("");

                // break out
                break;                
              } else {
                // not yet 3 digits
                Serial.write(bell);
                
                // get next char;
                continue;
              }; // end else - if
              
          } else {
             // unknown char
             Serial.write(bell);
             
             // get next char
             continue;
          }; // end else - elsif - if

      }; // end state 12;

}; // end while


// Send PAGE
if (state == -1) {
  Serial.print("address: ");
  Serial.println(address);

  Serial.print("addresssource: ");
  Serial.println(addresssource);

  Serial.print("callsign: ");
  Serial.println(call);

  Serial.print("repeat: ");
  Serial.println(repeat);

  Serial.print("message: ");
  Serial.println(textmsg);


  // create pocsag message
  // batch2 option 1 (duplicate message)
  // invert option 1 (invert)

  rc=create_pocsag(address,addresssource,textmsg,&msg,1,1);


  if ((rc != 1) && (rc != 2)) {
    Serial.print("Error in createpocsag: return-code: ");
    Serial.println(rc);

  } else {
  
    //  pocsagmsg_correctendian(msg);
  
    if (rc == 1) {
      // 1 batch: sync (72 octets) + (17 * 4)
      size=140; 
    } else {
      // 2 batches: sync (72 octets) + 2 * (17 * 4)
      size=208;
    }; // end if


    // send at least once + repeat
    for (int l=-1; l < repeat; l++) {
      Serial.println("POCSAG SEND");

      rf22.send((uint8_t *)&msg, 208);
      rf22.waitPacketSent();
      
      delay(3000);
    }; // end for

  }; // end else - if; 


  // send CW ID: switch to 2000 bps GMSK and send short or long
  // dummy packets to create FM-modulated CW

  // switch to 2000 bps GMSK
  rf22.setModemConfig(RF22::GFSK_Rb2Fd5);

  sendcwid(call);
  
  // switch back to 512 bps FSK
  rf22.setModemRegisters(&MC);
  
}; // end function P (page)



// function "F": change frequency

if (state == -2) {
  float newfreq;

  newfreq=(float)freq1+((float)freq2/10000.0F); // f1 = MHz, F2 = 100 Hz
  

  if ((newfreq >= 430) && (newfreq <= 440)) {
    Serial.print("switching to new frequency: ");
    Serial.println(newfreq);
    
    freq=newfreq;

    rf22.setFrequency(freq, 0.00);
    
  } else {
    Serial.print("Error: invalid frequency: ");
    Serial.println(newfreq);
  }; // end if
}; // end function F (frequency)


}; // end main loop function


//////////////////////////////////////////////////////////////
//   ////////////////
//   End of main loop
///////





// create_pocsag

// creates pocsag message in pocsagmsg structure
// returns 1 or 2, if the message is one or two batches
int create_pocsag (long int address, int source, char * text, struct pocsagmsg * pocsagmsg, int option_batch2, int option_invert) {
int txtlen;
unsigned char c; // tempory var for character being processed
int stop; /// temp var

unsigned char txtcoded[56]; // encoded text can be up to 56 octets
int txtcodedlen;

// local vars to encode address line
int currentframe;
uint32_t addressline;

// counters to encode text
int bitcount_in, bitcount_out, bytecount_in, bytecount_out;

// some sanity checks
txtlen=strlen(text);
if (txtlen > 40) {
  // messages can be up to 40 chars (+ terminating EOT)
  txtlen=40;
}; // end if

// replace terminating \0 by EOT
text[txtlen]=0x04; // EOT (end of transmission)
txtlen++; // increase size by 1 (for EOT)

// some sanity checks for the address
// addreess are 21 bits
if ((address > 0x1FFFFF) || (address <= 0)) {
  return(-1);
}; // end if

// source is 2 bits
if ((source < 0) || (source > 3)) {
  return(-2);
}; // end if

// option "batch2" goes from 0 to 2
if ((option_batch2 < 0) || (option_batch2 > 2)) {
  return(-3);
}; // end if

// option "invert" should be 0 or 1
if ((option_invert < 0) || (option_invert > 1)) {
  return(-4);
}; // end if

// create packet

// A POCSAG packet has the following structure:
// - transmission syncronisation (576 bit "01" pattern)

// one or multiple "batches".
// - a batch starts with a 32 frame syncronisation pattern
// - followed by 8 * 2 * 32 bits of actual data or "idle" information:
//				the "POCSAG message"


// A POSAG message consists of one or multiple batches. A batch is 64
// octets: 8 frames of 2 codewords of 32 bits each

// This application can generate one pocsag-message containing one
// text-message of up to 40 characters + a terminating EOT
// (end-of-transmission, ASCII 0x04).

// the message (i.e. the destination-address + the text messate itself)
// always starts in the first batch but not necessairy at the beginning
// of the batch. The start-position of message inside the batch is
// determined by (the 3 lowest bits of) the address.

// if the message reaches the end of the first batch, if overflows
// into the 2nd batch.

// As a result, we do not know beforehand if the POCSAG frame will be
// one or two POCSAG message, so we will initialise both batches. If
// -and the end of the process- it turns out that the message fits into
// one single batch, there are three options:
// - truncate the message and send a POCSAG-frame containing one POCSAG-
// 	message
// - repeat the first batch in the 2nd batch  and send both batches
// - leave the 2nd batch empty (i.e. filled with the "idle" pattern) and
// 	send that

// note that before actually transmitting the pattern, all bits are inverted 0-to-1
// for the parts of the frame with a fixed text (syncronisation pattern), the
// bits are inverted in the code

// part 0.1: frame syncronisation pattern
if (option_invert == 0) {
  memset(pocsagmsg->sync,0xaa,72);
} else {
  memset(pocsagmsg->sync,0x55,72); // pattern 0x55 is invers of 0xaa
}; // end else - if

// part 0.2: batch syncronisation

// a batch begins with a sync codeword
// 0111 1100 1101 0010 0001 0101 1101 1000
pocsagmsg->synccw1[0]=0x7c;  pocsagmsg->synccw1[1]=0xd2;
pocsagmsg->synccw1[2]=0x15;  pocsagmsg->synccw1[3]=0xd8;

pocsagmsg->synccw2[0]=0x7c;  pocsagmsg->synccw2[1]=0xd2;
pocsagmsg->synccw2[2]=0x15;  pocsagmsg->synccw2[3]=0xd8;

// invert bits if needed
if (option_invert == 1) {
  pocsagmsg->synccw1[0] ^= 0xff; pocsagmsg->synccw1[1] ^= 0xff;
  pocsagmsg->synccw1[2] ^= 0xff; pocsagmsg->synccw1[3] ^= 0xff;

  pocsagmsg->synccw2[0] ^= 0xff; pocsagmsg->synccw2[1] ^= 0xff;
  pocsagmsg->synccw2[2] ^= 0xff; pocsagmsg->synccw2[3] ^= 0xff;
};

// part 0.3: init batches with all "idle-pattern"
for (int l=0; l< 16; l++) {
  pocsagmsg->batch1[l]=0x7a89c197;
  pocsagmsg->batch2[l]=0x7a89c197;
}; // end for


// part 1:
// address line:
// the format of a pocsag codeword containing an address is as follows:
// 0aaaaaaa aaaaaaaa aaassccc ccccccp
// "0" = fixed (indicating an codeword containing an address)
// a[18] = 18 upper bits of the address
// s[2]  = 2 bits source (encoding 4 different "address-spaces")
// c[10] = 10 bits of crc
// p = parity bit

// the lowest 3 bits of address are not encoded in the address line as
// found in the POCSAG message. However, they are used to determine
// where in the POCSAG-message, the message is started
currentframe = ((address & 0x7)<<1);


addressline=address >> 3;

// add address source
addressline<<=2;
addressline += source;

//// replace address line
replaceline(pocsagmsg,currentframe,createcrc(addressline<<11));





// part 2.1: 
// convert text to podsag format

// POCSAG codewords containing a message have the following format:
// 1ttttttt tttttttt tttttccc ccccccp
// "1" = fixed (indicating a codeword containing an encoded message)
// t[20] = 20 bits of text
// c[10] = 10 bits CRC
// p = parity bit

// text-messages are encoded using 7 bits ascii, IN INVERTED BITORDER (MSB first)

// As up to 20 bits can be stored in a codeword, each codeword contains 3 chars
//    minus 1 bit. Hence, the text is "wrapped" around to the next codewords


// The message is first encoded and stored into a tempory array

// init vars
// clear txtcoded
memset(txtcoded,0x00,56); // 56 octets, all "0x00"

bitcount_out = 7; // 1 char can contain up to 7 bits
bytecount_out = 0;

bitcount_in = 7;
bytecount_in=0;
c=flip7charbitorder(text[0]); // we start with first char


txtcodedlen=0;
txtcoded[0] = 0x80; // output, initialise as empty with  leftmost bit="1"



// loop for all chars
stop=0;

while (!stop) {
  int bits2copy;
  unsigned char t; // tempory var bits to be copied

  // how many bits to copy?
  // minimum of "bits available for input" and "bits that can be stored"
  if (bitcount_in > bitcount_out) {
    bits2copy = bitcount_out;
  } else {
    bits2copy = bitcount_in;
  }; // end if

	// copy "bits2copy" bits, shifted the left if needed
   t = c & (size2mask[bits2copy-1] << (bitcount_in - bits2copy) );

  // where to place ?
  // move left to write if needed
  if (bitcount_in > bitcount_out) {
    // move to right and export
    t >>= (bitcount_in-bitcount_out);
  } else if (bitcount_in < bitcount_out) {
    // move to left
    t <<= (bitcount_out-bitcount_in);
  }; // end else - if

  // now copy bits
  txtcoded[txtcodedlen] |= t;

  // decrease bit counters
  bitcount_in -= bits2copy;
  bitcount_out -= bits2copy;


  // outbound queue is full
  if (bitcount_out == 0) {
    // go to next position in txtcoded
    bytecount_out++;
    txtcodedlen++;

    // data is stored in codeblocks, each containing 20 bits with usefull data
    // The 1st octet of every codeblock always starts with a "1" (leftmost char)
    // to indicate the codeblock contains data
    // The 1st octet of a codeblock contains 8 bits: "1" + 7 databits
    // The 2nd octet of a codeblock contains 8 bits: 8 databits
    // the 3th octet of a codeblock contains 5 bits: 5 databits

    if (bytecount_out == 1) {
      // 2nd char of codeword:
      // all 8 bits used, init with all 0
      txtcoded[txtcodedlen]=0x00;
      bitcount_out=8;
    } else if (bytecount_out == 2) {
      // 3th char of codeword:
      // only 5 bits used, init with all 0
      txtcoded[txtcodedlen]=0x00;
      bitcount_out=5;
    } else if (bytecount_out >= 3) {
      // 1st char of codeword (wrap around of "bytecount_out" value)
      // init with leftmost bit as "1"
      txtcoded[txtcodedlen]=0x80;
      bitcount_out = 7;

      // wrap around "bytecount_out" value
      bytecount_out = 0;
    }; // end elsif - elsif - if

  }; // end if

  // ingress queue is empty
  if (bitcount_in == 0) {
    bytecount_in++;

    // any more text ?
    if (bytecount_in < txtlen) {
      // reinit vars
      c=flip7charbitorder(text[bytecount_in]);
      bitcount_in=7; // only use 7 bits of every char
    } else {
      // no more text
      // done, so ... stop!!
      stop=1;

      continue;
    }; // end else - if
  }; // end if
}; // end while (stop)

// increase txtcodedlen. Was used as index (0 to ...) above. Add one to
// make it indicate a lenth
txtcodedlen++;



// Part 2.2: copy coded message into pocsag message

// now insert text message
for (int l=0; l < txtcodedlen; l+= 3) {
  uint32_t t;

  // move up to next frame
  currentframe++;

  // copying is done octet per octet to be architecture / endian independant
  t = (uint32_t) txtcoded[l] << 24; // copy octet to bits 24 to 31 in 32 bit struct
  t |= (uint32_t) txtcoded[l+1] << 16; // copy octet to bits 16 to 23 in 32 bit struct
  t |= (uint32_t) txtcoded[l+2] << 11; // copy octet to bits 11 to 15 in 32 bit struct

  replaceline(pocsagmsg,currentframe,createcrc(t));

  // note: move up 3 chars at a time (see "for" above)
}; // end for


// invert bits if needed
if (option_invert) {
  for (int l=0; l<16; l++) {
    pocsagmsg->batch1[l] ^= 0xffffffff;
  }; // end for

  for (int l=0; l<16; l++) {
    pocsagmsg->batch2[l] ^= 0xffffffff;
  }; // end for
  
}; 


/// convert to make endian/architecture independant


for (int l=0; l<16; l++) {
  int32_t t1; 
  
  // structure to convert int32 to architecture  / endian independant 4-char array
  struct int32_4char t2;

  // batch 1
  t1=pocsagmsg->batch1[l];
  
  // left more octet
  t2.c[0]=(t1>>24)&0xff; t2.c[1]=(t1>>16)&0xff;
  t2.c[2]=(t1>>8)&0xff; t2.c[3]=t1&0xff;

  // copy back
  pocsagmsg->batch1[l]=t2.i;

  // batch 2
  t1=pocsagmsg->batch2[l];
  
  // left more octet
  t2.c[0]=(t1>>24)&0xff; t2.c[1]=(t1>>16)&0xff;
  t2.c[2]=(t1>>8)&0xff; t2.c[3]=t1&0xff;

  // copy back
  pocsagmsg->batch2[l]=t2.i;

}; // end for

  

// done
// return

// If only one single batch used
if (currentframe < 16) {
  // batch2 option:
  // 0: truncate to one batch
  // 1: copy batch1 to batch2
  // 2: leave batch2 as "idle"

  if (option_batch2 == 0) {
    // done. Just return indication length 1 batch
    return(1);
  } else if (option_batch2 == 1) {
    memcpy(pocsagmsg->batch2,pocsagmsg->batch1,64); //  16 codewords of 32 bits
  }; // end of

  // return for option_batch2 == 1 or 2
  // return indication length 2 batches
  return(2);
};


// return indication length 2 batches
return(2);


}; // end function create_pocsag






/////////////////////////////////////////////
// function sendcwid

void sendcwid (char *msg) {
int l,l2, s;
int n_morse;
  
n_morse=sizeof(morsetab) / sizeof(morsetab[0]);

// the id message should be 8 char long.
// some more validity checks to be added later

s=strlen(msg);
for (l=0; l < s; l++) {

   char c;
   int m;
   int state;
    
   // init as invalid
   c='?'; m=0;
    
   for (l2=0; l2<n_morse; l2++) {

      if (morsetab[l2].c == msg[l]) {
         c=msg[l];
         m=morsetab[l2].pat & 0xff;
        
        // break out
        l2=n_morse;
     }; // end if
   }; // end for
    
   Serial.print("CW ID Sending ");
   Serial.println(c);

   // if we have a valid char
   if (m) {
      int siz;
      int m2;
      
      // find left most "1"
      m2=m;
      
      siz=7;
      
      // move left until we find the left most bit
      while (!(m2 & 0x80)) {
         siz--;
         m2<<=1;
      }; // end while


      for (l2=0; l2 < siz; l2++) {

         // if "1" send long, else send short
         if (m & 0x01) {
            rf22.send(cwidmsg, CWID_LONG);rf22.waitPacketSent();
         } else {
            rf22.send(cwidmsg, CWID_SHORT);rf22.waitPacketSent();
         }; // end else - if
      
         // move pattern to left
         // end state=1
         m >>= 1;
      }; // end for (morse bits) 
   }; // end else - if (space ?)

   // wait for 350 ms between letters
   delay(350);  
    
}; // end for


}; // end function sendcwid


/////////////////////////////
/// function replaceline

void replaceline (struct pocsagmsg * msg, int line, uint32_t val) {

// sanity checks
if ((line < 0) || (line > 32)) {
  return;
}; // end if


if (line < 16) {
  msg->batch1[line]=val;
} else {
  msg->batch2[line-16]=val;
}; // end if


}; // end function replaceline


//////////////////////////
// function flip7charbitorder

unsigned char flip7charbitorder (unsigned char c_in) {
int i;
char c_out;

// init
c_out=0x00;

// copy and shift 6 bits
for (int l=0; l<6; l++) {
  if (c_in & 0x01) {
    c_out |= 1;
  }; // end if

  // shift data
  c_out <<= 1; c_in >>= 1;
}; // end for

// copy 7th bit, do not shift
if (c_in & 0x01) {
  c_out |= 1;
}; // end if

return(c_out);

}; // end function flip2charbitorder

//////////////////////////////
/// function createcrc

uint32_t createcrc(uint32_t in) {

// local vars
uint32_t cw; // codeword

uint32_t local_cw = 0;
uint32_t parity = 0;

// init cw
cw=in;

// move up 11 bits to make place for crc + parity
local_cw=in;  /* bch */

// calculate crc
for (int bit=1; bit<=21; bit++, cw <<= 1) {
  if (cw & 0x80000000) {
    cw ^= 0xED200000;
  }; // end if
}; // end for

local_cw |= (cw >> 21);

// parity
cw=local_cw;

for (int bit=1; bit<=32; bit++, cw<<=1) {
  if (cw & 0x80000000) {
    parity++;
  }; // end if
}; // end for


// make even parity
if (parity % 2) {
  local_cw++;
}; // end if

// done
return(local_cw);


}; // end function createcrc

