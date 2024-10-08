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

__attribute__((naked)) 
void callee_add_function() {
    //TODO: any prologue-epilogue part breaks this
  
    //Read cross-domain operands from caller's domain
    asm volatile (
        ".word 0x02200120\n\t"  //cldg x9, [cr0] (operand_1) 
        ".word 0x02200141\n\t" //cldg x10, [cr1] (operand_2)
    );
    asm volatile (
        "add x0, x9, x10\n\t"
    );
    //Return the caller
    asm volatile (
        ".word 0x02E00000" //cret
    );
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
        execl("./receiver-caller-cap", "receiver-caller-cap", NULL);
        // If execl fails
        perror("execl");
        exit(EXIT_FAILURE);
    } else {  // Parent process: Sender-Callee
        close(pipe_fd[0]);  // Close the read end of the pipe
        int spid=getpid();
        printf("Sender-Callee: Parent PID: %d\n", spid);

        cdom code_cap;
        // Set up necessary context to let the caller execute  
        code_cap.PC = (uint64_t)&callee_add_function;  
        code_cap.SP = ((uint64_t)malloc(4096));      // Safe custom stack pointer (SP) pointing heap buffer
        code_cap.SP=+4096;                           
        code_cap.PT = getPTBase();               
        code_cap.MAC= 0;

        // Write the context (PC, SP, and TTBR0) to the pipe
        write(pipe_fd[1], &code_cap, sizeof(cdom));
        
        printf("Sender-Callee: Sent PC value: 0x%lx\n", code_cap.PC);
        printf("Sender-Callee: Sent SP value: 0x%lx\n", code_cap.SP);
        printf("Sender-Callee: Sent PT base register (TTBR0_EL1) value: 0x%lx\n", code_cap.PT);
        printf("Sender-Callee: Sent domain cap with MAC value: 0x%lx\n", code_cap.MAC);

        // Close the pipe write end
        close(pipe_fd[1]);

        // Sleep to allow the receiver to process
        sleep(20);
    }

    return 0;
}
