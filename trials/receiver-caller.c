#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>

int main() {

    uint64_t pc;
    uint64_t sp; 
    uint64_t ttbr0;

    int rpid=getpid();

    printf("Receiver-Caller: Receiver PID: %d\n", rpid);

    read(STDIN_FILENO, &pc, sizeof(pc));
    read(STDIN_FILENO, &sp, sizeof(sp));
    read(STDIN_FILENO, &ttbr0, sizeof(ttbr0));
    
    
    printf("Receiver-Caller: Received PC value: 0x%lx\n", pc);
    printf("Receiver-Caller: Received SP value: 0x%lx\n", sp);
    printf("Receiver-Caller: Received TTBR0_EL1 value: 0x%lx\n", ttbr0);


    ///////////////////////////CROSS-DOMAIN FUNCTION CALL/////////////////////////////#

    // Inline assembly to load x0, x1, and x2 with ttbr0, pc, and sp
    asm volatile (
        "mov x0, %0\n"    // Load ttbr0 into x0
        "mov x1, %1\n"    // Load pc into x1
        "mov x2, %2\n"    // Load csp into x2
        :                 // No output
        : "r"(ttbr0), "r"(pc), "r"(sp)  // Input values to be placed into registers
        : "x0", "x1", "x2"  // Clobber list - letting compiler know x0, x1, x2 will be modified
    );
    
    //CCALL
    asm volatile (
        //ccall
        ".word 0x02D00000\n\t"
    );

    //the callee's cret via cjmp should jump to here. 
    printf("Receiver-Caller: Cross-domain function is successfully returned to the caller (cret)!\n");
    
    return 0;
}
