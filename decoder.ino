/**
 *   Decodes POCSAG data from the FSK demodulator output on the RF board of Motorola pagers.
 *   Tested successfully with up to 1200 baud POCSAG

 *   Copyright (C) 2013  Tom de Waha

 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *   You should have received a copy of the GNU General Public License along
 *   with this program; if not, write to the Free Software Foundation, Inc.,
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
**/

#include <TimerOne.h>
#define receiverPin 3
#define triggerPin 6
#define ledPin 13
#define bitPeriod 833
#define halfBitPeriod 416
#define prmbWord 1431655765
#define syncWord 2094142936
#define idleWord 2055848343

#define STATE_WAIT_FOR_PRMB 0
#define STATE_WAIT_FOR_SYNC 1
#define STATE_PROCESS_BATCH 2
#define STATE_PROCESS_MESSAGE 3

static const char *functions[4] = {"Function A", "Function B","Function C", "Function D"};

volatile unsigned long buffer = 0;
volatile int bitcounter = 0;
int state = 0;
int wordcounter = 0;
int framecounter = 0;
int batchcounter = 0;

unsigned long wordbuffer[81];

void setup()
{
  pinMode(receiverPin, INPUT);
  pinMode(triggerPin, OUTPUT);
  disable_led();
  disable_trigger();
  start_flank();
  Timer1.initialize(bitPeriod);  
}

void loop()
{
   switch(state)
   {
     case STATE_WAIT_FOR_PRMB:
       if(buffer==prmbWord)
       {
         state = STATE_WAIT_FOR_SYNC;
       }
     break;
     
     case STATE_WAIT_FOR_SYNC:
       if(buffer==syncWord)
       {
          bitcounter = 0;
          state = STATE_PROCESS_BATCH;
       } else {
          if(bitcounter > 540)    // SYNC word did not appear, going back to idle mode
          {
            bitcounter = 0;
            if(batchcounter > 0)
            {
              state = STATE_PROCESS_MESSAGE;
            } else {
              state = STATE_WAIT_FOR_PRMB; 
            }
            batchcounter = 0;
          } 
       }
     break;
     
     case STATE_PROCESS_BATCH: 
       if(bitcounter >= 32)
       {
          bitcounter = 0;
          wordbuffer[(batchcounter * 16)+(framecounter*2)+wordcounter] = buffer;
          wordcounter++;
       }
       
       if(wordcounter >= 2)
       {
         wordcounter = 0;
         framecounter++;
       }
       
       if(framecounter >= 8)
       {
         framecounter = 0;
         batchcounter++;
         state = STATE_WAIT_FOR_SYNC;
       }
       
       if(batchcounter >= 5)
       {
         batchcounter = 0;
         state = STATE_PROCESS_MESSAGE;     
       }      
     break;
     
     case STATE_PROCESS_MESSAGE:
       stop_flank();
       stop_timer();
       
       decode_wordbuffer();

       memset(wordbuffer,0,sizeof(wordbuffer));
       state = STATE_WAIT_FOR_PRMB;

       start_flank();
     break;
   }   
}

void decode_wordbuffer()
{
  unsigned long address = 0;
  byte function = 0;
  char message[160];
  memset(message,0,sizeof(message));
  byte character = 0;
  int bcounter = 0;
  int ccounter = 0;
  boolean eot = false;
  
  for(int i=0;i<81;i++)
  {
     if(parity(wordbuffer[i]) == 1) continue;                       // Invalid Codeword
     if(wordbuffer[i] == idleWord) continue;                        // IDLE
     if(wordbuffer[i] == 0) continue;                               // Empty Codeword
     
     if(bitRead(wordbuffer[i], 31) == 0)                            // Found an Address
     {
       if(address == 0)
       {
          address = extract_address(i);
          function = extract_function(i);
          eot = false;        
       }
     } else {
       if(address != 0 && ccounter < 160)
       {
         for(int c = 30; c > 10; c--)
         {
            bitWrite(character, bcounter, bitRead(wordbuffer[i], c));
            bcounter++;
            if(bcounter >= 7)
            {
               if(character == 4) eot=true;
               bcounter = 0;
               if(eot==false)
               {
                 message[ccounter] = character;
                 ccounter++; 
               }
            }
         }      
       } 
     }      
  }
  if(address != 0)
  {
     print_message(address, function, message); 
  }
}

void print_message(unsigned long address, byte function, char message[160])
{
  Serial.begin(115200);
  if(address<1000000) Serial.print("0");
  Serial.print(address);
  Serial.print("\t");
  Serial.print(functions[function]);
  Serial.print("\t");
  Serial.println(message);
  Serial.end();
}

unsigned long extract_address(int idx)
{
  unsigned long address = 0;
  int pos = (idx - (idx/8) * 8)/2;
  for(int i = 1; i < 19; i++)
  {
     bitWrite(address, 21-i, bitRead(wordbuffer[idx], 31-i));   
  }
  bitWrite(address, 0, bitRead((pos), 0));
  bitWrite(address, 1, bitRead((pos), 1));
  bitWrite(address, 2, bitRead((pos), 2));
 
  return address;  
}

byte extract_function(int idx)
{
   byte function = 0;
   bitWrite(function, 0, bitRead(wordbuffer[idx], 11));
   bitWrite(function, 1, bitRead(wordbuffer[idx], 12));
   return function;   
}

void flank_isr()
{
  delayMicroseconds(halfBitPeriod-20);
  start_timer();   
}

void timer_isr()
{
  buffer = buffer << 1;                               
  bitWrite(buffer, 0, bitRead(PIND,3));
  if(state > STATE_WAIT_FOR_PRMB) bitcounter++; 
}

void start_timer()
{
  Timer1.restart();
  Timer1.attachInterrupt(timer_isr);
}

void stop_timer()
{
  Timer1.detachInterrupt();
}

void start_flank()
{
  attachInterrupt(1, flank_isr, RISING);
}

void stop_flank()
{
  detachInterrupt(1); 
}




