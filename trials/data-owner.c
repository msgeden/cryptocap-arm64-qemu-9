#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include "common.h"


//Data to be accessed by/added within Callee
uint64_t operand_1=111;
uint64_t operand_2=889;

int main() {

    cdom ccall_cap;
    
    uint64_t PT_base=getPTBase();
    uint64_t PT_Kernelbase=getKernelPTBase();
    uint64_t TTBREL1;

    uint64_t SP_Kernel=getKernelSP();
    
    int a=0;
    int b=0;
    int sum=0;

    int rpid=getpid();

    printf("***********************************************************\n");

    printf("Caller-Data Owner: PID: %d\n", rpid);

    read(STDIN_FILENO, &ccall_cap, sizeof(cdom));
    
    /////////////////////////////PREPARE TARGET REGISTER FOR CCALL /////////////////////////////#
    printf("Caller-Data Owner: Setting the target register (CLC)\nCLC.PC=0x%lx CLC.SP=0x%lx, CLC.PTBASE=0x%lx, CLC.MAC=0x%lx\n", ccall_cap.PC, ccall_cap.SP, ccall_cap.PT, ccall_cap.MAC);
    
    // Load ccall_cap into the target (CLC) register
    asm volatile (
        "mov x9, %0\n\t"    // Load the address of ccall_cap into x9
        :                 // no output
        : "r"(&ccall_cap) // input
        : "x9"            // clobber list 
    );
    
    // Load ccall_cap into CLC register
    asm volatile (
        ".word 0x03900009\n\t"    // cldc clc, [x9] 
    );


    /////////////////////////////PREPARE CAPABILITY REGISTER ARGUMENTS FOR CCALL/////////////////////////////#
  
    printf("Caller-Data Owner: Setting CR0 and CR1 registers as function arguments to provide cross-domain access to two operands\n");
    printf("Caller-Data Owner: Caller-hosted Operand-1=%ld\n",operand_1);
    printf("Caller-Data Owner: Caller-hosted Operand-2=%ld\n",operand_2);
    
    // // Set cr0.base via x9
    asm volatile (
        "mov x9, %0\n\t"
        ".word 0x02500009\n\t"      // csetbase cr0, cr0, x9
        :
        :"r"(&operand_1)
        : "x9" // clobber list 
    );

    // Set cr0.perms (RWXT) via x9
    __asm__ volatile (
        "mov x9, #0xF\n\t"       // mov x9, #0xF
        ".word 0x02700009\n\t"      // csetperms cr0, cr0, x9
    );

    // Set cr0.size info (8 bytes) via x9
    __asm__ volatile (
        "mov x9, #0x8\n\t"   // mov x0, #0x00000008
        ".word 0x02600009\n\t"      // csetsize cr0, cr0, x9
    );

    // Set cr0.offset (0) via x9
    __asm__ volatile (
        "mov x9, #0x0\n\t"   // mov x0, #0x0
        ".word 0x02800009\n\t"      // csetaddr cr1, cr0, x9
    );

    // Set cr0.PT via x9
    __asm__ volatile (
        "mov x9, %0\n\t"           
        ".word 0x03400009\n\t"      // csetpt cr0, cr0, x9
        :                        // no output 
        : "r" (PT_base)          // input
        : "x9"                   // clobber list
    );

    //Move/Copy cr0 to cr1
    __asm__ volatile (
        ".word 0x03000020\n\t"      // cmov   cr1, cr0
    );

    // Change base address (operand_1) of cr1 to the address of operand_2
    asm volatile (
        "mov x9, %0\n\t"
        ".word 0x02500429\n\t"      // csetbase cr1, cr1, x9
        :
        :"r"(&operand_2)
        : "x9"            // clobber list 
    );

    ///////////////////////////PERFORM THE CALL/////////////////////////////#
   
    asm volatile(
        "ldr x1, %[var]"      // Load the value of stack_variable into x1
        :                     // No output operands
        : [var] "m" (TTBREL1)  // Input operand (memory reference to stack_variable)
        : "x1"                // Clobbers (x1 register is modified)
    );

    //Call to sender-callee process
    asm volatile (
        //ccall
        ".word 0x02D00000\n\t"
    );

    asm volatile (
    "mov %0, x0"   // Save the result from x0 into variable 'sum'
        : "=r" (sum)        // Output operand
        :                   // No input operands
        : "x0"        // Clobbered registers
    );

    //the callee's cret via cjmp should jump to here. 
    printf("Caller-Data Owner: Successful return from 'addition' function that accessed operands via cross-domain caps.\nSum: %d\n",sum);
    
    return 0;
}
