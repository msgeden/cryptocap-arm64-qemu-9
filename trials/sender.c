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

    if (pid == 0) {  // Child process: Receiver
        close(pipe_fd[1]);  // Close the write end of the pipe
        printf("Sender: Child PID: %d\n", pid);

        // Execute the receiver program
        dup2(pipe_fd[0], STDIN_FILENO);  // Redirect pipe read end to stdin
        //execl("/bin/busybox", "busybox", "sh", "-c", "./receiver-arm64", NULL);
        execl("./receiver", "receiver", NULL);
        // If execl fails
        perror("execl");
        exit(EXIT_FAILURE);
    } else {  // Parent process: Sender
        close(pipe_fd[0]);  // Close the read end of the pipe
        int spid=getpid();
        printf("Sender: Parent PID: %d\n", spid);

        // Simulate the data to send (e.g., pointer and register value)
        uint64_t *sent_heap_addr = (uint64_t *)malloc(sizeof(uint64_t));
        if (sent_heap_addr == NULL) {
            perror("malloc failed");
            exit(EXIT_FAILURE);
        }
        uint64_t sent_heap_value=42;
        *sent_heap_addr = sent_heap_value;

        uint64_t sent_stack_value=52;
        uint64_t* sent_stack_addr=&sent_stack_value;
       
        uint64_t sent_pt_base_addr = get_ttbr0_el1();

        printf("Sender: Sent heap address: %p\n", sent_heap_addr);
        printf("Sender: Sent heap value: %ld\n", sent_heap_value);
        printf("Sender: Sent stack address: %p\n", sent_stack_addr);
        printf("Sender: Sent stack value: %ld\n", sent_stack_value);
        printf("Sender: Sent PT base register (TTBR0_EL1) value: 0x%lx\n", sent_pt_base_addr);
       
        // Write the simulated data to the pipe
        write(pipe_fd[1], &sent_heap_addr, sizeof(sent_heap_addr));
        write(pipe_fd[1], &sent_heap_value, sizeof(sent_heap_value));
        write(pipe_fd[1], &sent_stack_addr, sizeof(sent_stack_addr));
        write(pipe_fd[1], &sent_stack_value, sizeof(sent_stack_value));
        write(pipe_fd[1], &sent_pt_base_addr, sizeof(sent_pt_base_addr));
        
        // Close the pipe write end
        close(pipe_fd[1]);

        // Sleep to allow the receiver to process
        sleep(20);
    }

    return 0;
}
