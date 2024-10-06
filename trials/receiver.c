#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>

int main() {

    uint64_t *received_heap_addr;
    uint64_t received_heap_value; 
    
    uint64_t received_stack_value;
    uint64_t *received_stack_addr; 
    
    uint64_t received_pt_base_addr;

    uint64_t received_heap_value_via_cap=40; //to check this value does not appear
    uint64_t received_stack_value_via_cap=50; //to check this value does not appear
    
    int rpid=getpid();

    printf("Receiver: Receiver PID: %d\n", rpid);

    read(STDIN_FILENO, &received_heap_addr, sizeof(received_heap_addr));
    read(STDIN_FILENO, &received_heap_value, sizeof(received_heap_value));
    read(STDIN_FILENO, &received_stack_addr, sizeof(received_stack_addr));
    read(STDIN_FILENO, &received_stack_value, sizeof(received_stack_value));
    read(STDIN_FILENO, &received_pt_base_addr, sizeof(received_pt_base_addr));
    
    printf("Receiver: Received heap address: %p\n", (void *)received_heap_addr);
    printf("Receiver: Received heap value: %ld\n", received_heap_value);
    printf("Receiver: Received stack address: %p\n", (void *)received_stack_addr);
    printf("Receiver: Received stack value: %ld\n", received_stack_value);
    printf("Receiver: Received PT base register (TTBR0_EL1): 0x%lx\n", received_pt_base_addr);



    ///////////////////////////CROSS-DOMAIN STACK VARIABLE/////////////////////////////#
    
    // Load the received_addr into register x1
    __asm__ volatile (
        "mov x1, %0\n"           // mov x1, variable_value
        :                        // No output operands
        : "r" (received_stack_addr)   // Input operand: variable_value
        : "x1"                   // Clobbers x1 register
    );
    // Set cr0/1.base via x0
    __asm__ volatile (
        "mov x0, x1\n"            // mov x0, x1
        ".word 0x02500400\n"      // csetbase cr1, cr0, x0
        ".word 0x03000001\n"      // cmov   cr0, cr1
    );

    // Load the received_pt_base into register x1
    __asm__ volatile (
        "mov x1, %0\n"           // mov x1, variable_value
        :                        // No output operands
        : "r" (received_pt_base_addr)   // Input operand: variable_value
        : "x1"                   // Clobbers x1 register
    );
    // Set cr0/1.PT cr0/1 via x0
    __asm__ volatile (
        "mov x0, x1\n"            // mov x0, x1
        ".word 0x03400400\n"      // csetpt cr1, cr0, x0
        ".word 0x03000001\n"      // cmov   cr0, cr1
    );

    // Load size info (16 bytes) into register x0
    __asm__ volatile (
        "mov x0, #0x00000020\n"   // mov x0, #0x00000020
    );
    // Set cr0/1.size via x0
    __asm__ volatile (
        ".word 0x02600400\n"      // csetsize cr1, cr0, x0
        ".word 0x03000001\n"      // cmov   cr0, cr1
    );


    // Load (RWXT) perms into register x0
    __asm__ volatile (
        "mov x0, #0x000F\n"       // mov x0, #0x000F
    );
    // Set cr0/1.perms for via x0
    __asm__ volatile (
        ".word 0x02700400\n"      // csetperms cr1, cr0, x0
        ".word 0x03000001\n"      // cmov   cr0, cr1
    );


    // Load offset info (0) into register x0
    __asm__ volatile (
        "mov x0, #0x00000000\n"   // mov x0, #0x00000000
    );
    // Set cr0/1.offset via x0
    __asm__ volatile (
        ".word 0x02800400\n"      // csetaddr cr1, cr0, x0
        ".word 0x03000001\n"      // cmov   cr0, cr1
    );

    
    // Sign cr1 (capability) register for cross-domain access (this will be performed by privileged handlers later)
    __asm__ volatile (
        ".word 0x02900001\n"      // csign cr1
    );


    // Load 64-bit value from receiver's address sapce into x4 register via signed cr1
    __asm__ volatile (
        ".word 0x02200081\n"      // cldg x4, [cr1]
    );
    // Store x4 value the address of the received_value_via_cap
    __asm__ volatile (
        "str x4, %0\n"           // Store the value from x4 into variable_value
        : "=m" (received_stack_value_via_cap)  // Output operand: the variable in memory
        :                        // No input operands
        : "x4"                   // Clobbers x4 register
    );

    //Print for confirmation
    printf("Receiver: Received stack value via cross-domain cap dereference: %ld\n", received_stack_value_via_cap);
    


    /////////////////////CROSS-DOMAIN HEAP VARIABLE///////////
     // Load the received_addr into register x1
    __asm__ volatile (
        "mov x1, %0\n"           // mov x1, variable_value
        :                        // No output operands
        : "r" (received_heap_addr)   // Input operand: variable_value
        : "x1"                   // Clobbers x1 register
    );
    // Set cr0/1.base via x0
    __asm__ volatile (
        "mov x0, x1\n"           // mov x0, x1
        ".word 0x02500400\n"      // csetbase cr1, cr0, x0
        ".word 0x03000001\n"      // cmov   cr0, cr1
    );

     // Load the received_pt_base into register x1
    __asm__ volatile (
        "mov x1, %0\n"           // mov x1, variable_value
        :                        // No output operands
        : "r" (received_pt_base_addr)   // Input operand: variable_value
        : "x1"                   // Clobbers x1 register
    );
    // Set cr0/1.PT cr0/1 via x0
    __asm__ volatile (
        "mov x0, x1\n"            // mov x0, x1
        ".word 0x03400400\n"      // csetpt cr1, cr0, x0
        ".word 0x03000001\n"      // cmov   cr0, cr1
    );

    // Load size info (16 bytes) into register x0
    __asm__ volatile (
        "mov x0, #0x00000020\n"   // mov x0, #0x00000020
    );
    // Set cr0/1.size via x0
    __asm__ volatile (
        ".word 0x02600400\n"      // csetsize cr1, cr0, x0
        ".word 0x03000001\n"      // cmov   cr0, cr1
    );

    // Load (RWXT) perms into register x0
    __asm__ volatile (
        "mov x0, #0x000F\n"       // mov x0, #0x000F
    );
    // Set cr0/1.perms for via x0
    __asm__ volatile (
        ".word 0x02700400\n"      // csetperms cr1, cr0, x0
        ".word 0x03000001\n"      // cmov   cr0, cr1
    );

    // Load offset info (0) into register x0
    __asm__ volatile (
        "mov x0, #0x00000000\n"   // mov x0, #0x00000000
    );
    // Set cr0/1.offset via x0
    __asm__ volatile (
        ".word 0x02800400\n"      // csetaddr cr1, cr0, x0
        ".word 0x03000001\n"      // cmov   cr0, cr1
    );

    // Sign cr1 (capability) register for cross-domain access (this will be performed by privileged handlers later)
    __asm__ volatile (
        ".word 0x02900001\n"      // csign cr1
    );

    // Load 64-bit value from receiver's address sapce into x4 register via signed cr1
    __asm__ volatile (
        ".word 0x02200081\n"      // cldg x4, [cr1]
    );
    
    // Store x4 value the address of the received_value_via_cap
    __asm__ volatile (
        "str x4, %0\n"           // Store the value from x4 into variable_value
        : "=m" (received_heap_value_via_cap)  // Output operand: the variable in memory
        :                        // No input operands
        : "x4"                   // Clobbers x4 register
    );

    //Print for confirmation
    printf("Receiver: Received heap value via cross-domain via cap dereference: %ld\n", received_heap_value_via_cap);
    
    return 0;
}
