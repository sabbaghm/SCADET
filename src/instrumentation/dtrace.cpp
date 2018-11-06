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
 *  This file contains an ISA-portable PIN tool for tracing memory accesses.
 */

/*
Note:
The instrumentation code is based on the "pinatrace" pintool.
The virtual to physical address translation code is based on the following:
http://fivelinesofcode.blogspot.com/2014/03/how-to-translate-virtual-to-physical.html
*/


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <stdint.h>
//#include <hugetlbfs.h>

#include "pin.H"

//#define PS 2048*1024
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

struct Pindata
{
//	uint64_t instruction_pointer;
	uint64_t read_write; //1: read, 0: write
//	uint64_t virtual_address;
//	uint64_t instruction_pointer;
	uint64_t physical_address; 
//	uint64_t access_time_stamp;
} pindata;


//uint64_t pindata[4];

//FILE * tracetest;
FILE * trace;

KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool",
    "o", "pinatrace.out", "specify output file name");

static __inline__ uint64_t timenow (void);

uint64_t get_frame_number_from_pagemap(uint64_t value);

uint64_t va2pa(uint64_t virt_addr);

// Print a memory read record
VOID RecordMemRead(VOID * ip, VOID * addr);

// Print a memory write record
VOID RecordMemWrite(VOID * ip, VOID * addr);

// Is called for every instruction and instruments reads and writes
VOID Instruction(INS ins, VOID *v);

VOID Fini(INT32 code, VOID *v);

/* ===================================================================== */
/* Print Help Message                                                    */
/* ===================================================================== */
   
INT32 Usage();

/* ===================================================================== */
/* Main                                                                  */
/* ===================================================================== */

int main(int argc, char *argv[])
{
    if (PIN_Init(argc, argv)) return Usage();

    char path_buf[] = "/proc/self/pagemap";
   
    //printf("Big endian? %d\n", is_bigendian());
    f = fopen(path_buf, "rb");
    if(!f){
       printf("Error! Cannot open %s\n", path_buf);
       return -1;
    }

    //tracetest = fopen("test.out", "w");
    trace = fopen(KnobOutputFile.Value().c_str(), "wb");

    INS_AddInstrumentFunction(Instruction, 0);
    PIN_AddFiniFunction(Fini, 0);

    // Never returns
    PIN_StartProgram();
    
    fclose(f); 
    return 0;
}

static __inline__ uint64_t timenow (void) {
		unsigned cycles_low, cycles_high;
		asm volatile("RDTSCP\n\t"
					"mov %%edx, %0\n\t"
					"mov %%eax, %1\n\t"
					"CPUID\n\t": "=r" (cycles_high), "=r" (cycles_low):: "%rax",
					"%rbx", "%rcx", "%rdx");
		return ((uint64_t)cycles_high << 32) | cycles_low;
}

uint64_t get_frame_number_from_pagemap(uint64_t value)
{
  return value & ((1ULL << 55) - 1);
}

uint64_t va2pa(uint64_t virt_addr){ 
   
   //Shifting by virt-addr-offset number of bytes
   //and multiplying by the size of an address (the size of an entry in pagemap file)
   //file_offset = virt_addr / getpagesize() * PAGEMAP_ENTRY;
   file_offset = (virt_addr / getpagesize()) * PAGEMAP_ENTRY;
   //printf("Vaddr: 0x%lx, Page_size: %d, Entry_size: %d\n", virt_addr, getpagesize(), PAGEMAP_ENTRY);
//   printf("Reading %s at 0x%llx\n", path_buf, (unsigned long long) file_offset);
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
         //printf("\nReached end of the file\n");
         return 0;
      }
      if(is_bigendian())
           c_buf[i] = c;
      else
           c_buf[PAGEMAP_ENTRY - i - 1] = c;
      //printf("[%d]0x%x ", i, c);
   }
   for(i=0; i < PAGEMAP_ENTRY; i++){
      //printf("%d ",c_buf[i]);
      read_val = (read_val << 8) + c_buf[i];
   }
   //printf("\n");
   //assert(read_val & (1ULL << 63));
   //printf("Result: 0x%lx\n", (uint64_t) read_val);
   //if(GET_BIT(read_val, 63))
   if(~GET_BIT(read_val, 63))
      //printf("PFN: 0x%lx\n",(uint64_t) GET_PFN(read_val));
   //else
      {
      	//printf("Page not present\n");
      	return 0;
      }
   if(GET_BIT(read_val, 62))
      printf("Page swapped\n");
   uint64_t frame_num = get_frame_number_from_pagemap(read_val);
   //return (frame_num * getpagesize()) | (virt_addr & (getpagesize()-1));
   return ( (frame_num * getpagesize()) + (virt_addr % getpagesize()) );
}



// Print a memory read record
VOID RecordMemRead(VOID * ip, VOID * addr)
{
    //fprintf(trace,"%p: R %p\n", ip, addr);
    pindata.read_write = 1;
    //pindata.instruction_pointer = (uint64_t)ip; //add later for code back anotation or replace it with a counter
    //pindata.virtual_address = (uint64_t)addr;
    pindata.physical_address = va2pa((uint64_t)addr);
    //pindata.access_time_stamp = timenow();
    //fprintf(tracetest,"R,%p,%p,%lu,%lu\n", ip, addr, va2pa((uint64_t) addr), pindata.access_time_stamp);
    fwrite(&pindata, sizeof(pindata), 1, trace);
}

// Print a memory write record
VOID RecordMemWrite(VOID * ip, VOID * addr)
{
    //fprintf(trace,"%p: W %p\n", ip, addr);
    pindata.read_write = 0;
    //pindata.instruction_pointer = (uint64_t)ip; //add later for code back anotation or replace it with a counter
    //pindata.virtual_address = (uint64_t)addr;
    pindata.physical_address = va2pa((uint64_t)addr);
    //pindata.access_time_stamp = timenow();
    //fprintf(tracetest,"W,%p,%p,%lu,%lu\n", ip, addr, va2pa((uint64_t) addr), pindata.access_time_stamp);
    fwrite(&pindata, sizeof(pindata), 1, trace);
}

// Is called for every instruction and instruments reads and writes
VOID Instruction(INS ins, VOID *v)
{
    // Instruments memory accesses using a predicated call, i.e.
    // the instrumentation is called iff the instruction will actually be executed.
    //
    // On the IA-32 and Intel(R) 64 architectures conditional moves and REP 
    // prefixed instructions appear as predicated instructions in Pin.
    UINT32 memOperands = INS_MemoryOperandCount(ins);

    // Iterate over each memory operand of the instruction.
    for (UINT32 memOp = 0; memOp < memOperands; memOp++)
    {
        if (INS_MemoryOperandIsRead(ins, memOp))
        {
            INS_InsertPredicatedCall(
                ins, IPOINT_BEFORE, (AFUNPTR)RecordMemRead,
                IARG_INST_PTR,
                IARG_MEMORYOP_EA, memOp,
                IARG_END);
        }
        // Note that in some architectures a single memory operand can be 
        // both read and written (for instance incl (%eax) on IA-32)
        // In that case we instrument it once for read and once for write.
        if (INS_MemoryOperandIsWritten(ins, memOp))
        {
            INS_InsertPredicatedCall(
                ins, IPOINT_BEFORE, (AFUNPTR)RecordMemWrite,
                IARG_INST_PTR,
                IARG_MEMORYOP_EA, memOp,
                IARG_END);
        }
    }
}


VOID Fini(INT32 code, VOID *v)
{
    //fprintf(trace, "#eof\n");
    //fclose(tracetest);
    fclose(trace);
}

/* ===================================================================== */
/* Print Help Message                                                    */
/* ===================================================================== */
   
INT32 Usage()
{
    PIN_ERROR( "This Pintool prints a trace of memory addresses\n" 
              + KNOB_BASE::StringKnobSummary() + "\n");
    return -1;
}

