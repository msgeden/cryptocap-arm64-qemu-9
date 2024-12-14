#include <stdio.h>
#include <unistd.h>

#include <sys/syscall.h>
#include <sys/wait.h>

#include "cc-common.h"
int lib2_operand=200;
int lib2_add(void)      
{
    int operand2=0; 
    int sum=0;
    asm volatile (
        ".word 0x02200141\n\t" //cldg x10, [cr1] (operand_2)
        //"mov x9, #4\n\t"
        //"mov x10, #9\n\t"
        "str x10, %0\n\t"          // Store x9 into memory at address of a
        :
        : "m" (operand2)   // Output operands for x9 and x10
        :"x10"
    );
    sum=operand2+lib2_operand;
    printf("Lib2: Lib2 returning to Lib1 with value:%d\n", sum);
    syscall(SYS_pret, sum);
    exit(0);
}

int main(void)
{
    int pipe_fd[2];  // [0] for reading, [1] for writing

    // Create the pipe
    if (pipe(pipe_fd) == -1) {
        perror("pipe");
        exit(EXIT_FAILURE);
    }

    pid_t pid = fork();
    
    if (pid == 0) {

        // Application process
        close(pipe_fd[1]);  // Close the write end of the pipe

        // Execute the Caller-Data Owner program
        dup2(pipe_fd[0], STDIN_FILENO);  // Redirect pipe read end to stdin
        execl("./lib1", "lib1", NULL);
        // If execl fails
        perror("execl");
        exit(EXIT_FAILURE);

    } else {
        // library process
        close(pipe_fd[0]);  // close the read end of the pipe
        
        int lib_pid=getpid();
        
        // share the necessary info with the application process  
        pcap lib2_proces_cap;
        lib2_proces_cap.PC = (uint64_t)&lib2_add;  
        lib2_proces_cap.PID = lib_pid;
        lib2_proces_cap.MAC = 0;
     
        // Write the necessary callee info (PC, SP, and TTBR0) to the pipe
        write(pipe_fd[1], &lib2_proces_cap, sizeof(pcap));

        printf("Lib2: PID: %d\n", lib_pid);
        printf("Lib2: Sent Target PID: %ld\n", lib2_proces_cap.PID);
        printf("Lib2: Sent Target Address: 0x%lx\n", lib2_proces_cap.PC);
        printf("Lib2: Sent MAC value: 0x%lx\n", lib2_proces_cap.MAC);
        
        wait_for_call_via_loop();    

     }

    return 0;
}