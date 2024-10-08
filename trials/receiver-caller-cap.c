#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>

typedef struct ccap {
     uint64_t perms_base;
     uint32_t offset;
     uint32_t size;
     uint64_t PT;
     uint64_t MAC;
} ccap;
typedef struct cdom {
     uint64_t PC;
     uint64_t SP;
     uint64_t PT;
     uint64_t MAC;
} cdom;

uint64_t getPTBase() {
    uint64_t value=0xDEADBEEF;
    asm volatile(".word 0x03300000" : "=r"(value));  //readttbr x0
    return value;
}

//Data to be accessed by/added within another domain
uint64_t operand_1=100;
uint64_t operand_2=90;

int main() {

    cdom ccall_cap={0,0,0,0};
    cdom cret_cap={0,0,0,0};
    
    uint64_t PT_base=getPTBase();
    
    uint64_t* op1_addr=&operand_1;
    uint64_t* op2_addr=&operand_2;

    int a=0;
    int b=0;
    int sum=0;

    int rpid=getpid();

    printf("Receiver-Caller: Receiver PID: %d\n", rpid);

    read(STDIN_FILENO, &ccall_cap, sizeof(cdom));
    
    printf("Receiver-Caller: Received PC value: 0x%lx\n", ccall_cap.PC);
    printf("Receiver-Caller: Received SP value: 0x%lx\n", ccall_cap.SP);
    printf("Receiver-Caller: Received PT base register (TTBR0_EL1) value: 0x%lx\n", ccall_cap.PT);
    printf("Receiver-Caller: Received MAC value: 0x%lx\n", ccall_cap.MAC);

    ///////////////////////////PREPARE CLC TARGET REGISTER FOR CROSS-DOMAIN CCALL////////////#

    // Inline assembly to load ccall_cap into CLC (ccall target) register
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

    
    // ///////////////////////////PREPARE DATA CAPABILITY ARGUMENTS/////////////////////////////#
    // // Set cr0.base via x9
    asm volatile (
        "mov x9, %0\n\t"
        ".word 0x02500009\n\t"      // csetbase cr0, cr0, x9
        :
        :"r"(op1_addr)
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
        :"r"(op2_addr)
        : "x9"            // clobber list 
    );
    //local access test via cap
    asm volatile (
         ".word 0x02200120\n\t"  //cldg x9, [cr0] (operand_1) 
         ".word 0x02200141\n\t" //cldg x10, [cr1] (operand_2)
    );

     asm volatile (
    "mov %0, x9\n\t"
    "mov %1, x10"
        : "=r" (a), "=r"(b)        // Output operand
        :                   // No input operands
        : "x9", "x10"        // Clobbered registers
    );

    printf("Receiver-Caller: Intra-domain access to operands via caps.\nOperand 1: %d, Operand 2: %d\n",a,b);

    // // Sign cr1 (capability) register for cross-domain access (this will be performed by privileged handlers later)
    // __asm__ volatile (
    //     ".word 0x02900000\n"      // csign cr0
    //     ".word 0x02900001\n"      // csign cr1
    // );

    ///////////////////////////PERFORM THE CALL/////////////////////////////#

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
    printf("Receiver-Caller: Addition function that accessed the operands via cross-domain caps successfully returned.\nSum: %d\n",sum);
    
    return 0;
}
