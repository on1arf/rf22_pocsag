// SENDPOCSAG TEXT-message version
// creates pocsag message and send the 2 batches over the serial port
// to the atmega to play out


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




// for write
#include <stdio.h>

// for open
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

// for memset
#include <string.h>

// term io: serial port
#include <termios.h>

// for EXIT
#include <unistd.h>
#include <stdlib.h>

// for errno
#include <errno.h>

// for flock
#include <sys/file.h>

// for uint32_t
#include <stdint.h>



// functions defined below
uint32_t createcrc (uint32_t);
void replaceline (int, uint32_t);
unsigned char invertbits (unsigned char);


// some constants
const unsigned char size2mask[7]={ 0x01, 0x03, 0x07, 0x0f, 0x1f, 0x3f, 0x7f }; 


// global data (shared between functions)
// memory to store two pocsag batches
uint32_t batch1[16];
uint32_t batch2[16];




///////////////////////////////////////////
///////// MAIN APPLICATION
/////


int main (int argc, char ** argv) {

unsigned char txtin[41]; // text can be up to 40 chars, add one for EOT
unsigned char txtcoded[42]; // coded version of text message (reserve 42 chars, a multiple of 3)

int txtlen;
int txtcodedlen;

int address;
int addresssource;





// serial device
// termios: structure to configure serial port
struct termios tio;
char * serialdevice = "/dev/ttyAMA0";
int serialfd;


// input to read
char buffin[64];

char * pocgo1 = "POCGO1";
char * pocend = "POCEND";


char callsign[9]; // callsign: up to 8 chars + null
unsigned char config[6];
// configuration currently used:
// config[0]:0 send FSK id
// config[0]:1 send CW id

// other vars
int ret;
int l; // loop

int currentframe;
int addressline;


// parse parameters
if (argc < 5) {
	fprintf(stderr,"Missing paramter: need at least 4 parameters: callsign, address, address-source,  text\n");
	exit(-1);
}; // end if

// copy up to 40 chars 
txtlen=strlen(argv[4]);

// max 40 chars
if (txtlen > 40) {
	txtlen=40;
}; // end if

// copy text
memcpy(txtin,argv[4],txtlen);

// replace terminating \0 by EOT
txtin[txtlen]=0x04; // EOT (end of transmission)
txtlen++; // increase size by 1 (for EOT)



address = atoi(argv[2]);

// sanity check: address is 21 bits
if ((address >= (2^21)) || (address <= 0)) {
	fprintf(stderr,"Error: invalid address, should be between 1 and (2^21)-1, got %d\n",address);
	exit(-1);
}; // end if




addresssource = atoi(argv[3]);

// sanity check: address is 2 bits
if ((addresssource > 3) || (address < 0)) {
	fprintf(stderr,"Error: invalid address-source, should be between 0 and 3, got %d\n",addresssource);
	exit(-1);
}; // end if



// init config
memset(config,0x0,6);
config[0] |= 0x03;

// copy callsign from CLI param 1, up to 8 chars allows
memset(callsign,0x00,9); // init
strncpy(callsign,argv[1],8);


serialfd=open(serialdevice, O_RDWR | O_NONBLOCK);

if (serialfd == 0) {
	fprintf(stderr,"Error: could not open serial device %s \n",serialdevice);
}; // end if

// make exclusive lock
ret=flock(serialfd,LOCK_EX | LOCK_NB);

if (ret != 0) {
	int thiserror=errno;

	fprintf(stderr,"Exclusive lock to serial port failed. (%s) \n",strerror(thiserror));
	fprintf(stderr,"Serial device not found or already in use\n");
	exit(-1);
}; // end if

// Serial port programming for Linux, see termios.h for more info
memset(&tio,0,sizeof(tio));
tio.c_iflag=0; tio.c_oflag=0; tio.c_cflag=CS8|CREAD|CLOCAL; // 8n1
tio.c_lflag=0; tio.c_cc[VMIN]=1; tio.c_cc[VTIME]=0;

cfsetospeed(&tio,B9600); // 9.6 Kbps
cfsetispeed(&tio,B9600); // 9.6 Kbps
tcsetattr(serialfd,TCSANOW,&tio);


// create packet
// first fill up batch 1 with all idle

for (l=0; l< 16; l++) {
	batch1[l]=0x7a89c197;
	batch2[l]=0x7a89c197;
}; // end for



// Convert text message
// read from txtin, store in txtcoded

{
// local vars

int stop;

int bitcount_in, bitcount_out;
int bytecount_in, bytecount_out;

unsigned char c_in; // input char being processed
unsigned char t; // tempory var


// init some vars
// clear all txtcodec
for (l=0; l<42; l++) {
	txtcoded[l]=0x00;
}; // end for

// init bitcount_out at 7 (instead of normally 8) as we will fill up the first char with a leftmost 1
bitcount_out=7;
bytecount_out=0;

bitcount_in=7;
bytecount_in=0;

// character is first char (from left)
c_in=invertbits(txtin[bytecount_in]);

txtcodedlen=0;
txtcoded[0] = 0x80; // output, initialise as empty with  leftmost bit="1"

// loop for all data
stop=0;

while (stop == 0) {
	int bit2copy;

	
	// how many bits to copy?
	// look for smallest available
	if (bitcount_in > bitcount_out) {
		bit2copy = bitcount_out;
	} else {
		bit2copy = bitcount_in;
	}; // end if

	// maximum is 7
	if (bit2copy > 7) {
		bit2copy = 7;
	}; // end if

	// read input, copy "x" bits, shifted to left if needed
	t = c_in & (size2mask[bit2copy-1] << (bitcount_in - bit2copy) );

	// where to place ?
	// move left to write if needed
	if (bitcount_in > bitcount_out) {
		// move to right and export
		t >>= (bitcount_in-bitcount_out);
	} else if (bitcount_in < bitcount_out) {
		// move to left
		t <<= (bitcount_out-bitcount_in);
	}; // end else - if

	// insert data in egress stream
	txtcoded[txtcodedlen] |= t;

	// decrease bit_counters
	bitcount_in -= bit2copy;
	bitcount_out -= bit2copy;

	// sanity checks
	if (bitcount_in < 0) {
		fprintf(stderr,"Error: negative bitcount_in: %d \n",bitcount_in);
		exit(-1);
	}; // end if


	if (bitcount_out < 0) {
		fprintf(stderr,"Error: negative bitcount_out: %d \n",bitcount_out);
		exit(-1);
	}; // end if


	// flush output if end reached
	if (bitcount_out == 0) {
		bytecount_out++;
		txtcodedlen++;

		// data is stored in codeblocks, each containing 20 bits with usefull data
		// The 1st octet of every codeblock always starts with a "1" (leftmost char)
		// to indicate the codeblock contains data
		// The 1st octet of a codeblock contains 8 bits: 1 fixed "1" + 7 databits

		// The 2nd octet of a codeblock contains 8 bits: 8 databits
		// the 3th octet of a codeblock contains 5 bits: 5 databits

		// the actual text to be transmitted is encoded in 7 bits + terminating "EOT"

		if (bytecount_out == 1) {
			// 2nd char of codeword:
			// all 8 bits, init with all 0
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

			// wrap around
			bytecount_out = 0;
		}; // end elsif - elsif - if

	}; // end if


	// read new input if ingress queue empty
	if (bitcount_in == 0) {
		bytecount_in++;

		// still data in txtbuffer
		if (bytecount_in < txtlen) {
			// reinit vars
			c_in=invertbits(txtin[bytecount_in]);
			bitcount_in=7;
		} else {
			// no more data in ingress queue
			// done, so ... stop!!
			stop=1;

			continue;
		}; // end else - if (still data in queue?)
	}; // end if (ingress queue empty?)

}; //  end while (stop?)

// increase txtcodedlen. Was used as index (0 to ...) above. Add one to
// make it indicate a lenth
txtcodedlen++;


}; // end subblock convert




// address field: 
// lowest 3 bits of address determine startframe
currentframe = ((address & 0x7)<<1);


addressline=address >> 3;

// add address source
addressline<<=2;
addressline += addresssource;

// replace address line
replaceline(currentframe,createcrc(addressline<<11));


// now insert text message
for (l=0; l < txtcodedlen; l+= 3) {
	uint32_t t;

	// move up to next frame
	currentframe++;

	t = (uint32_t) txtcoded[l] << 24; // copy octet to bits 24 to 31 in 32 bit struct
	t |= (uint32_t) txtcoded[l+1] << 16; // copy octet to bits 16 to 23 in 32 bit struct
	t |= (uint32_t) txtcoded[l+2] << 11; // copt octet to bits 11 to 15 in 32 bit struct

	replaceline(currentframe,createcrc(t));

	// note: move up 3 chars at a time (see "for" above)
}; // end for



// part 1: send "POCGO1"

ret=write(serialfd,pocgo1,6);
if (ret != 6) {
	printf("write did not return correct value, got %d \n",ret);
}; // end if


// write batches

for (l=0; l<16; l++) {
	// copy octet per octet to be platform / endian endependant

	char c;
	c=(batch1[l] >> 24) & 0xff; ret=write(serialfd,&c,1);
	c=(batch1[l] >> 16) & 0xff; ret=write(serialfd,&c,1);
	c=(batch1[l] >> 8) & 0xff; ret=write(serialfd,&c,1);
	c=(batch1[l]) & 0xff; ret=write(serialfd,&c,1);
}; // end for



// 2nd batch
for (l=0; l<16; l++) {
	// copy octet per octet to be platform / endian endependant

	char c;
	c=(batch2[l] >> 24) & 0xff; ret=write(serialfd,&c,1);
	c=(batch2[l] >> 16) & 0xff; ret=write(serialfd,&c,1);
	c=(batch2[l] >> 8) & 0xff; ret=write(serialfd,&c,1);
	c=(batch2[l]) & 0xff; ret=write(serialfd,&c,1);
}; // end for

// callsign
ret=write(serialfd,callsign,8);
if (ret != 8) {
	printf("write did not return correct value, got %d \n",ret);
}; // end if

// config
ret=write(serialfd,config,6);
if (ret != 6) {
	printf("write did not return correct value, got %d \n",ret);
}; // end if


// done
ret=write(serialfd,pocend,6);
if (ret != 6) {
	printf("write did not return correct value, got %d \n",ret);
}; // end if



ret=read (serialfd,buffin,64);
if (ret < 0) {
	int thiserror=errno;

	fprintf(stderr,"read from serial failed: (%s) \n",strerror(thiserror));
	exit(-1);
}; // end if

while (ret > 0) {
	// add terminating \n
	buffin[ret]=0;
	printf("%s\n",buffin);

	// read next
	ret=read (serialfd,buffin,64);
}; // end while


exit(0);
}; // end MAIN



///////////////////////////////////////////////////////
//////// END MAIN APPLICATION //////////////////////
////////////////////////////////////////////////



//// function  createcrc
uint32_t createcrc (uint32_t val) {

// local vars
uint32_t cw; // codeword

int bit=0;
int local_cw = 0;
int parity = 0;


// init cw
cw=val;

// move up 11 bits to make place for crc + parity
local_cw=val;  /* bch */

for (bit=1; bit<=21; bit++, cw <<= 1) {
	if (cw & 0x80000000) {
		cw ^= 0xED200000;
	}; // end if
}; // end for

local_cw |= (cw >> 21);

cw =local_cw;  /* parity */

for (bit=1; bit<=32; bit++, cw<<=1) {
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





//// function replaceline
void replaceline (int line, uint32_t val) {

// sanity checks
if ((line < 0) || (line > 32)) {
	return;
}; // end if


if (line < 16) {
	batch1[line]=val;
} else {
	batch2[line-16]=val;
}; // end if


}; // end function replaceline;


unsigned char invertbits (unsigned char c_in) {
int l;
char c_out;

// reduce to 7 bits
c_in &= 0x7f;


// init
c_out=0x00;

// copy and shift 6 bits
for (l=0; l<6; l++) {
	if (c_in & 0x01) {
		c_out |= 1;
	}; // end if
	c_out <<= 1;
	c_in >>= 1;
}; // end for

// copy 7th bit, do not shift
if (c_in & 0x01) {
	c_out |= 1;
}; // end if

return(c_out);

}; // end function invertbits

