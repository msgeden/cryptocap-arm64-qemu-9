#include <stdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>

#include <sys/syscall.h>
#include <sys/wait.h>

#include "cc-common.h"

//Data to be accessed by/added within Callee
uint64_t app_hosted_operand_1=111;
uint64_t app_hosted_operand_2=889;

int main(void)
{
    //Performance of a dummy syscall
    pcap process_cap;
    uint64_t PT_base=getPTBase();
   
    int app_pid=getpid();

   
    read(STDIN_FILENO, &process_cap, sizeof(pcap));
    
    /////////////////////////////PREPARE TARGET REGISTER FOR CCALL /////////////////////////////#
    printf("Application-Data Owner: PID: %d\n", app_pid);
    printf("Application-Data Owner: Received PID=%ld, PC=0x%lx, MAC=0x%lx\n", process_cap.PID, process_cap.PC, process_cap.MAC);

    /////////////////////////////PREPARE CAPABILITY REGISTER ARGUMENTS FOR CCALL/////////////////////////////#
  
    printf("Application-Data Owner: Setting CR0 and CR1 registers as function application-hosted arguments\n");
    printf("Application-Data Owner: Operand-1=%ld\n",app_hosted_operand_1);
    printf("Application-Data Owner: Operand-1=%ld\n",app_hosted_operand_2);
    
    // // Set cr0.base via x9
    asm volatile (
        "mov x9, %0\n\t"
        ".word 0x02500009\n\t"      // csetbase cr0, cr0, x9
        :
        :"r"(&app_hosted_operand_1)
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
        :"r"(&app_hosted_operand_2)
        : "x9"            // clobber list 
    );

    ///////////////////////////PERFORM THE CALL/////////////////////////////#

       
    asm volatile(
        "ldr x0, %[var]"      // Load the value of stack_variable into x1
        :                     // No output operands
        : [var] "m" (process_cap.PID)  // Input operand (memory reference to stack_variable)
        : "x0"                // Clobbers (x1 register is modified)
    );

    asm volatile(
        "ldr x1, %[var]"      // Load the value of stack_variable into x1
        :                     // No output operands
        : [var] "m" (process_cap.PC)  // Input operand (memory reference to stack_variable)
        : "x0"                // Clobbers (x1 register is modified)
    );

    // //Call to sender-callee process  (ccall)
    // asm volatile (
    //  "mov x8, #466\t\n"
    //  "svc #0\n" 
    // );

    int sum=0;
    printf("Application calling Library with\n");

    sum=syscall(SYS_pcall, process_cap.PID, process_cap.PC);
    printf("Library returned with result:%d\n",sum);

    return 0;
}
