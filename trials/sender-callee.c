#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>


// Function to get the TTBR0_EL1 register value (Page Table Base Register)
uint64_t get_ttbr0_el1() {
    uint64_t value=0xDEADBEEF;
    //asm volatile("mrs x0, ttbr0_el1" : "=r"(value)); 
    //read PT base register (TTBR0_EL1) value into x0  
    asm volatile(".word 0x03300000" : "=r"(value));  //readttbr x0
    //printf("get_ttbr0_el1(): 0x%lx\n", value);
    return value;
}

// Simulate the function that will be called by the caller
__attribute__((naked))
void callee_function() {
    //TODO: any function prologue-epilogue break the cross-domain-call for now
    //asm volatile(".word 0x03300004");  //readttbr x4
    //printf("Sender-Callee: Cross-domain function is successfully called by the caller (ccall)!\n");
    //CRET
    asm volatile (
        //cret
        ".word 0x02E00000"
    );
    //printf("Sender-Callee: Unreachable printf (There is a problem if you see this!!!)\n");
}

int main() {
    int pipe_fd[2];  // Pipe file descriptors: [0] for reading, [1] for writing

    // Create the pipe
    if (pipe(pipe_fd) == -1) {
        perror("pipe");
        exit(EXIT_FAILURE);
    }

    // Fork a child process to simulate the receiver
    pid_t pid = fork();
    
    if (pid == -1) {
        perror("fork");
        exit(EXIT_FAILURE);
    }

    if (pid == 0) {  // Child process: Receiver-Caller
        close(pipe_fd[1]);  // Close the write end of the pipe

        printf("Sender-Callee: Child PID: %d\n", pid);
        
        // Execute the receiver-caller program
        dup2(pipe_fd[0], STDIN_FILENO);  // Redirect pipe read end to stdin
        //execl("/bin/busybox", "busybox", "sh", "-c", "./receiver-arm64", NULL);
        execl("./receiver-caller", "receiver-caller", NULL);
        // If execl fails
        perror("execl");
        exit(EXIT_FAILURE);
    } else {  // Parent process: Sender-Callee
        close(pipe_fd[0]);  // Close the read end of the pipe
        int spid=getpid();
        printf("Sender: Parent PID: %d\n", spid);

        uint64_t pc, sp, ttbr0;
        // Set up the context to share (replace with actual values if necessary)
        pc = (uint64_t)&callee_function;  // Function address (PC)
        sp = (uint64_t)malloc(4096);      // Example stack pointer (SP) to safe custom stack in heap
        sp += 512;
        ttbr0 = get_ttbr0_el1();               // Example TTBR0_EL value

        // Write the context (PC, SP, and TTBR0) to the pipe
        write(pipe_fd[1], &pc, sizeof(pc));
        write(pipe_fd[1], &sp, sizeof(sp));
        write(pipe_fd[1], &ttbr0, sizeof(ttbr0));

        printf("Sender-Callee: Sent PC value: 0x%lx\n", pc);
        printf("Sender-Callee: Sent SP value: 0x%lx\n", sp);
        printf("Sender-Callee: Sent PT base register (TTBR0_EL1) value: 0x%lx\n", ttbr0);
        
        // Close the pipe write end
        close(pipe_fd[1]);

        // Sleep to allow the receiver to process
        sleep(20);
    }

    return 0;
}
