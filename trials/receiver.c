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

    printf("Receiver: Receviver PID: %d\n", rpid);

    read(STDIN_FILENO, &received_heap_addr, sizeof(received_heap_addr));
    read(STDIN_FILENO, &received_heap_value, sizeof(received_heap_value));
    read(STDIN_FILENO, &received_stack_addr, sizeof(received_stack_addr));
    read(STDIN_FILENO, &received_stack_value, sizeof(received_stack_value));
    read(STDIN_FILENO, &received_pt_base_addr, sizeof(received_pt_base_addr));
    
    printf("Receiver: Received heap address: %p\n", (void *)received_heap_addr);
    printf("Receiver: Received heap value: %ld\n", received_heap_value);
    printf("Receiver: Received stack address: %p\n", (void *)received_stack_addr);
    printf("Receiver: Received stack value: %ld\n", received_stack_value);
    printf("Receiver: Received PT base register: 0x%lx\n", received_pt_base_addr);



    ///////////////////////////STACK VARIABLE/////////////////////////////#
    
    // Load the received_addr into register x1
    __asm__ volatile (
        "mov x1, %0\n"           // mov x1, variable_value
        :                        // No output operands
        : "r" (received_stack_addr)   // Input operand: variable_value
        : "x1"                   // Clobbers x1 register
    );
    // Move the value from x1 to x0
    __asm__ volatile (
        "mov x0, x1\n"           // mov x0, x1
    );
    // Set base for cr0/1 via x0
    __asm__ volatile (
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
    // Move the value from x1 to x0
    __asm__ volatile (
        "mov x0, x1\n"           // mov x0, x1
    );
    // Set base for cr0/1 via x0
    __asm__ volatile (
        ".word 0x03400400\n"      // csetpt cr1, cr0, x0
        ".word 0x03000001\n"      // cmov   cr0, cr1
    );



    // Load dummy (16 bytes) size into register x0
    __asm__ volatile (
        "mov x0, #0x00000020\n"   // mov x0, #0x00000020
    );
    // Set size for cr0/1 via x0
    __asm__ volatile (
        ".word 0x02600400\n"      // csetsize cr1, cr0, x0
        ".word 0x03000001\n"      // cmov   cr0, cr1
    );



    // Load (WX) perms into register x0
    __asm__ volatile (
        "mov x0, #0x000F\n"       // mov x0, #0x000F
    );
    // Set perms for cr0/1 via x0
    __asm__ volatile (
        ".word 0x02700400\n"      // csetperms cr1, cr0, x0
        ".word 0x03000001\n"      // cmov   cr0, cr1
    );



    // Load dummy (0) offset into register x0
    __asm__ volatile (
        "mov x0, #0x00000000\n"   // mov x0, #0x00000000
    );
    // Set offset/addr for cr0/1 via x0
    __asm__ volatile (
        ".word 0x02800400\n"      // csetaddr cr1, cr0, x0
        ".word 0x03000001\n"      // cmov   cr0, cr1
    );


    
    // Sign capability register cr1
    __asm__ volatile (
        ".word 0x02900001\n"      // csign cr1
    );


    // Load value from receiver domain into x4 via signed capability cr1
    __asm__ volatile (
        ".word 0x02200081\n"      // cldg x4, [cr1]
    );
    
    // Assign the value in x4 to the received_value_via_cap
    __asm__ volatile (
        "str x4, %0\n"           // Store the value from x4 into variable_value
        : "=m" (received_stack_value_via_cap)  // Output operand: the variable in memory
        :                        // No input operands
        : "x4"                   // Clobbers x4 register
    );
    printf("Receiver: Received stack value via cap dereference: %ld\n", received_stack_value_via_cap);
    


    /////////////////////HEAP VARIABLE///////////
     // Load the received_addr into register x1
    __asm__ volatile (
        "mov x1, %0\n"           // mov x1, variable_value
        :                        // No output operands
        : "r" (received_heap_addr)   // Input operand: variable_value
        : "x1"                   // Clobbers x1 register
    );
    // Move the value from x1 to x0
    __asm__ volatile (
        "mov x0, x1\n"           // mov x0, x1
    );
    // Set base for cr0/1 via x0
    __asm__ volatile (
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
    // Move the value from x1 to x0
    __asm__ volatile (
        "mov x0, x1\n"           // mov x0, x1
    );
    // Set base for cr0/1 via x0
    __asm__ volatile (
        ".word 0x03400400\n"      // csetpt cr1, cr0, x0
        ".word 0x03000001\n"      // cmov   cr0, cr1
    );



    // Load dummy (16 bytes) size into register x0
    __asm__ volatile (
        "mov x0, #0x00000020\n"   // mov x0, #0x00000020
    );
    // Set size for cr0/1 via x0
    __asm__ volatile (
        ".word 0x02600400\n"      // csetsize cr1, cr0, x0
        ".word 0x03000001\n"      // cmov   cr0, cr1
    );



    // Load (WX) perms into register x0
    __asm__ volatile (
        "mov x0, #0x000F\n"       // mov x0, #0x000F
    );
    // Set perms for cr0/1 via x0
    __asm__ volatile (
        ".word 0x02700400\n"      // csetperms cr1, cr0, x0
        ".word 0x03000001\n"      // cmov   cr0, cr1
    );



    // Load dummy (0) offset into register x0
    __asm__ volatile (
        "mov x0, #0x00000000\n"   // mov x0, #0x00000000
    );
    // Set offset/addr for cr0/1 via x0
    __asm__ volatile (
        ".word 0x02800400\n"      // csetaddr cr1, cr0, x0
        ".word 0x03000001\n"      // cmov   cr0, cr1
    );


    
    // Sign capability register cr1
    __asm__ volatile (
        ".word 0x02900001\n"      // csign cr1
    );


    // Load value from receiver domain into x4 via signed capability cr1
    __asm__ volatile (
        ".word 0x02200081\n"      // cldg x4, [cr1]
    );
    
    // Assign the value in x4 to the received_value_via_cap
    __asm__ volatile (
        "str x4, %0\n"           // Store the value from x4 into variable_value
        : "=m" (received_heap_value_via_cap)  // Output operand: the variable in memory
        :                        // No input operands
        : "x4"                   // Clobbers x4 register
    );
    printf("Receiver: Received heap value via cap dereference: %ld\n", received_heap_value_via_cap);
    
    
    // Simulation: Print the received data
    printf("Receiver: Simulated access to shared_var\n");

    return 0;
}
