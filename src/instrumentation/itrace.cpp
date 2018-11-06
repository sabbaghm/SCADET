/*BEGIN_LEGAL 
Intel Open Source License 

Copyright (c) 2002-2017 Intel Corporation. All rights reserved.
 
Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.  Redistributions
in binary form must reproduce the above copyright notice, this list of
conditions and the following disclaimer in the documentation and/or
other materials provided with the distribution.  Neither the name of
the Intel Corporation nor the names of its contributors may be used to
endorse or promote products derived from this software without
specific prior written permission.
 
THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE INTEL OR
ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
END_LEGAL */

/*
Note:
The instrumentation code is based on the "itrace" pintool.
The virtual to physical address translation code is based on the following:
http://fivelinesofcode.blogspot.com/2014/03/how-to-translate-virtual-to-physical.html
*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <stdint.h>

#include "pin.H"

#define PAGEMAP_ENTRY 8
#define GET_BIT(X,Y) (X & ((uint64_t)1<<Y)) >> Y
#define GET_PFN(X) X & 0x7FFFFFFFFFFFFF

const int __endian_bit = 1;
#define is_bigendian() ( (*(char*)&__endian_bit) == 0 )

int i, c, pid, status;
uint64_t virt_addr;
uint64_t read_val, file_offset;
char path_buf [0x100] = {};
FILE * f;
char *end;

uint64_t instruction_pointer;
uint64_t items;

FILE * trace;

// Defining KNOBS
KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool",
    "o", "itrace.out", "specify output file name");


uint64_t get_frame_number_from_pagemap(uint64_t value);

uint64_t va2pa(uint64_t virt_addr);


// This function is called before every instruction is executed
// and prints the IP
VOID printip(VOID *ip) {
        instruction_pointer = va2pa((uint64_t)ip);
        fwrite(&instruction_pointer, sizeof(instruction_pointer), 1, trace);
}

// Pin calls this function every time a new instruction is encountered
VOID Instruction(INS ins, VOID *v)
{
    	// Insert a call to printip before every instruction, and pass it the IP
    	INS_InsertPredicatedCall(
        	                ins,
        	                IPOINT_BEFORE,
        	                (AFUNPTR)printip,
        	                IARG_INST_PTR,
        	                IARG_END);
}

// This function is called when the application exits
VOID Fini(INT32 code, VOID *v)
{
    //fprintf(trace, "#eof\n");
    fclose(trace);
}

/* ===================================================================== */
/* Print Help Message                                                    */
/* ===================================================================== */

INT32 Usage()
{
    PIN_ERROR("This Pintool prints the IPs of every instruction executed\n"
              + KNOB_BASE::StringKnobSummary() + "\n");
    return -1;
}

/* ===================================================================== */
/* Main                                                                  */
/* ===================================================================== */

int main(int argc, char * argv[])
{
    // Initialize pin
    if (PIN_Init(argc, argv)) return Usage();

    char path_buf[] = "/proc/self/pagemap";

    f = fopen(path_buf, "rb");
    if(!f){
       printf("Error! Cannot open %s\n", path_buf);
       return -1;
    }

    items = 0;

    // Open the trace file for binary write and extract the output file name
    trace = fopen(KnobOutputFile.Value().c_str(), "wb");

    // Register Instruction to be called to instrument instructions
    INS_AddInstrumentFunction(Instruction, 0);

    // Register Fini to be called when the application exits
    PIN_AddFiniFunction(Fini, 0);

    // Start the program, never returns
    PIN_StartProgram();

    fclose(f);

    return 0;
}


uint64_t get_frame_number_from_pagemap(uint64_t value)
{
  return value & ((1ULL << 55) - 1);
}

uint64_t va2pa(uint64_t virt_addr){

   //Shifting by virt-addr-offset number of bytes
   //and multiplying by the size of an address (the size of an entry in pagemap file)
   file_offset = (virt_addr / getpagesize()) * PAGEMAP_ENTRY;
   status = fseek(f, (uint64_t) file_offset, SEEK_SET);
   if(status){
      perror("Failed to do fseek!");
      return -1;
   }
   errno = 0;
   read_val = 0;
   unsigned char c_buf[PAGEMAP_ENTRY];
   for(i=0; i < PAGEMAP_ENTRY; i++){
      c = getc(f);
      if(c==EOF){
         return 0;
      }
      if(is_bigendian())
           c_buf[i] = c;
      else
           c_buf[PAGEMAP_ENTRY - i - 1] = c;
   }
   for(i=0; i < PAGEMAP_ENTRY; i++){
      read_val = (read_val << 8) + c_buf[i];
   }
   if(~GET_BIT(read_val, 63))
      {
        return 0;
      }
   if(GET_BIT(read_val, 62))
      printf("Page swapped\n");
   uint64_t frame_num = get_frame_number_from_pagemap(read_val);
   return ( (frame_num * getpagesize()) + (virt_addr % getpagesize()) );
}
