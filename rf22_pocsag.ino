// rf22_sendpocsag

// receive POCSAG-message + callsign + configuration data from serial port (raspberry pi)
// send message on 439.9875 Mhz using si4432-based "RF22" modules


// this program uses arduino rf22 libraries:
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


// increase maximum packet site
#define RF22_MAX_MESSAGE_LEN 255

#define MAXSYNCFAIL 40

#define CWID_SHORT 33 // 106 ms @ 1200 bps
#define CWID_LONG 90 // 320ms @ 1200 bps

#include <SPI.h>
#include <RF22.h>

// Singleton instance of the radio
RF22 rf22;

uint8_t pocsagmsg[208]; // 208 = 1 sync-patterns (576 bits/72 octets) +
                          // 2 batches (544 bits/68 octets)

uint8_t synctxt[6]; // contains either "POCGO1" or "POCEND"

uint8_t call[8]; // callsign
uint8_t config[6]; // 6 octets of configuration data
// currently used: config[0]:0 send/do not send FSK ID
//                 config[0]:1 send/do not send CW ID


RF22::ModemConfig MC;


uint8_t cwidmsg[CWID_LONG];


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


// functions defined below
void sendcwid(unsigned char * idtxt);



void setup() 
{
  
  Serial.begin(9600);
  if (rf22.init()) {
    Serial.println("RF22 init OK");
  } else {
    Serial.println("RF22 init FAILED");
  }; // end if

//  rf22.setFrequency(439.9875, 0.00);
  rf22.setFrequency(439.9625, 0.00); // for some reason, I need to tune to 439.9625 to transmit on 439.9875 Mhz


  // 512 baud, FSK, 4.5 Khz fd
  MC.reg_6e=0x04; MC.reg_6f=0x32; MC.reg_70=0x2c; MC.reg_58=0x80;
  MC.reg_72=0x04; MC.reg_71=0x22;

  MC.reg_1c=0x2B; MC.reg_20=0xa1; MC.reg_21=0xE0; MC.reg_22=0x10;
  MC.reg_23=0xc7; MC.reg_24=0x00; MC.reg_25=0x09; MC.reg_1f=0x03;
  MC.reg_69=0x60;
  
  //  MC.reg_1d=0x3c; MC.reg_1e=0x02; MC.reg_2a=0xff; 
  
  rf22.setModemRegisters(&MC); 


  // 100 mW TX power
  rf22.setTxPower(RF22_TXPOW_20DBM);
  

  // create packet
  // first 576 bits are sync pattern
  memset(&pocsagmsg[0],0x55,72); // pattern 0x55 is invers of 0xaa
  
  
  // both batches begin with sync codewords
  // 0111 1100 1101 0010 0001 0101 1101 1000
  pocsagmsg[72]=0x7c;  pocsagmsg[73]=0xd2;
  pocsagmsg[74]=0x15;  pocsagmsg[75]=0xd8;

  pocsagmsg[140]=0x7c; pocsagmsg[141]=0xd2;
  pocsagmsg[142]=0x15; pocsagmsg[143]=0xd8;

  // invert bits
  pocsagmsg[72] ^= 0xff;  pocsagmsg[73] ^= 0xff;
  pocsagmsg[74] ^= 0xff;  pocsagmsg[75] ^= 0xff;

  pocsagmsg[140] ^= 0xff; pocsagmsg[141] ^= 0xff;
  pocsagmsg[142] ^= 0xff; pocsagmsg[143] ^= 0xff;


  // the rest of the two batches are read from the serial port
  // see loop function below

  // message = 0b01010101
  memset(cwidmsg,0x55,CWID_LONG);

}

void loop()
{
//vars
int syncfail;



// print "READY" message
Serial.println("POCSAG READY");



// fake loop. allows breaking out on error
while (1) {  
  int l; // loop
  
  // part 1: receive 6 bytes from the serial port
// to fill up 

  for (l=0; l < 6; l++) {
    // loop until data on serial port
    while (Serial.available() == 0) {
    }; // end empty loop
  
    synctxt[l]=Serial.read();  
  }; // end for
  
  
  syncfail=0;
  // loop until we receive the text "POCBEG"
  while (! ((synctxt[0] == 'P') && (synctxt[1] == 'O') &&
            (synctxt[2] == 'C') && (synctxt[3] == 'G') &&
            (synctxt[4] == 'O') && (synctxt[5] == '1'))) {
    syncfail++;
    
    // break out if more then MAXSYNCFAIL sync-failed
    if (syncfail > MAXSYNCFAIL) {
      break;
    }; // end if
    
    // move up char, and get next char
    synctxt[0]=synctxt[1]; synctxt[1]=synctxt[2];
    synctxt[2]=synctxt[3]; synctxt[3]=synctxt[4];
    synctxt[4]=synctxt[5];
    
    // get next char from serial port
    while (Serial.available() == 0) {
    }; // end empty loop
  
    synctxt[5]=Serial.read();  
    
  }; // end while (sync pattern)

  // completely break out after too many syncfail
  if (syncfail > MAXSYNCFAIL) {
    Serial.println("POCSAG SYNCFAILED");
    break;
  }; // end if

  Serial.println("POCSAG START");

  Serial.println("POCSAG READ BATCH1");

  // part 2a, get 16 * 2 codewords (64 octets) for 1st batch
  for (l=76; l < 140; l++) {
    // get char from serial port
    while (Serial.available() == 0) {
    }; // end empty loop
  
    pocsagmsg[l]=Serial.read();  
    // invert bits
    pocsagmsg[l]  ^= 0xff;
  }; // end for

  Serial.println("POCSAG READ BATCH2");

  // part 2b, get 16 * 2 codewords (64 octets) for 2nd batch
  for (l=144; l < 208; l++) {
    // get char from serial port
    while (Serial.available() == 0) {
    }; // end empty loop
  
    pocsagmsg[l]=Serial.read();
    
    // invert bits
    pocsagmsg[l]  ^= 0xff;
  }; // end for
  
  
  Serial.println("POCSAG READ CALL");
  
  // part 2c: read callsign (8 chars)
  for (l=0; l < 8; l++) {
    // get char from serial port
    while (Serial.available() == 0) {
    }; // end empty loop
  
    call[l]=Serial.read();  
  }; // end for


  Serial.println("POCSAG READ CONFIG");

  // part 2d: get config data (6 octets)
  for (l=0; l < 6; l++) {
    // get char from serial port
    while (Serial.available() == 0) {
    }; // end empty loop
  
    config[l]=Serial.read();  
  }; // end for


  Serial.println("POCSAG READ ENDMARKER");
  
  // part 3: get end sync message (6 octets)
  for (l=0; l < 6; l++) {
    // get char from serial port
    while (Serial.available() == 0) {
    }; // end empty loop
  
    synctxt[l]=Serial.read();  
  }; // end for


  Serial.println("POCSAG ALLREAD");


  // do we have a correct end sync ("POCEND")
  if ((synctxt[0] == 'P') && (synctxt[1] == 'O') &&
            (synctxt[2] == 'C') && (synctxt[3] == 'E') &&
            (synctxt[4] == 'N') && (synctxt[5] == 'D')) {
    Serial.println("POCSAG END");


  // FSK ID. Just a FSK packet with your callsign
  // Fixed-length: 8 chars

  if (config[0] & 0x01) {
    Serial.println("POCSAG ID-FSK");

    rf22.send(call, 8);
    rf22.waitPacketSent();
      
    delay(500);
  }; // end if


  // send POCSAG message 3 times
  for (l=0; l < 3; l++) {
     Serial.println("POCSAG SEND");

      rf22.send(pocsagmsg, 208);
      rf22.waitPacketSent();
      
      delay(3000);
   }; // end for



  if (config[0] & 0x02) {
    // send CW ID: switch to 2000 bps GMSK and send short or long
    // dummy packets to create FM-modulated CW

    // switch to 2000 bps GMSK
    rf22.setModemConfig(RF22::GFSK_Rb2Fd5);

  
    sendcwid(call);
  
    // switch back to 512 bps FSK
    rf22.setModemRegisters(&MC);
  }; // end if
  



  } else {
    Serial.println("POCSAG INVALID");
  }; // end else - if
    
  // done, break out of fake endless loop
break;
}; // end fake endless loop

}; // end main function


//////////////////////////////////////////////////////////////
//   ////////////////
//   End of main function
///////



void sendcwid (unsigned char *msg) {
 int l,l2;
int n_morse;
  
n_morse=sizeof(morsetab) / sizeof(morsetab[0]);

// the id message should be 8 char long.
// some more validity checks to be added later


for (l=0; l < 8; l++) {

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


}; // end function cwidmsg
