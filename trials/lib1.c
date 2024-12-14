#include <stdio.h>
#include <unistd.h>

#include <sys/syscall.h>
#include <sys/wait.h>

#include "cc-common.h"
int lib1_operand=100;
pcap lib2_process_cap;

int lib1_add(void)      
{
   
    int operand1=0;
    int sum;
    asm volatile (
        ".word 0x02200120\n\t"  //cldg x9, [cr0] (operand_1) 
        //"mov x9, #4\n\t"
        //"mov x10, #9\n\t"
        "str x9, %0\n\t"          // Store x9 into memory at address of a
        :
        : "m" (operand1)   // Output operands for x9 and x10
        :"x9"
    ); 
    printf("Lib1: calling Lib2\n");
    sum=syscall(SYS_pcall, lib2_process_cap.PID, lib2_process_cap.PC);
    printf("Lib1: Lib2 returned with result:%d\n",sum);
    sum=sum+lib1_operand+operand1;
    printf("Lib1: Lib1 returning to App with value:%d\n", sum);
    syscall(SYS_pret, sum);
    exit(0);
}

int main(void)
{
    int pipe_fd[2];  // [0] for reading, [1] for writing   
    read(STDIN_FILENO, &lib2_process_cap, sizeof(pcap));
    
    /////////////////////////////PREPARE TARGET REGISTER FOR CCALL /////////////////////////////#
    printf("Lib1: PID: %d\n", getpid());
    printf("Lib1: Received PID=%ld, PC=0x%lx, MAC=0x%lx\n", lib2_process_cap.PID, lib2_process_cap.PC, lib2_process_cap.MAC);

    // Create the pipe
    if (pipe(pipe_fd) == -1) {
        perror("pipe");
        exit(EXIT_FAILURE);
    }

    pid_t fpid = fork();
    
    if (fpid == 0) {

        // Application process
        close(pipe_fd[1]);  // Close the write end of the pipe

        // Execute the Caller-Data Owner program
        dup2(pipe_fd[0], STDIN_FILENO);  // Redirect pipe read end to stdin
        execl("./app", "app", NULL);
        // If execl fails
        perror("execl");
        exit(EXIT_FAILURE);

    } else {
        // library process
        close(pipe_fd[0]);  // close the read end of the pipe
        
        int lib1_pid=getpid();
        
        
        // share the necessary info with the application process  
        pcap lib1_cap;
        lib1_cap.PC = (uint64_t)&lib1_add;  
        lib1_cap.PID = lib1_pid;
        lib1_cap.MAC = 0;
     
        // Write the necessary callee info (PC, SP, and TTBR0) to the pipe
        write(pipe_fd[1], &lib1_cap, sizeof(pcap));

        printf("Lib1: PID: %d\n", lib1_pid);
        printf("Lib1: Sent Target PID: %ld\n", lib1_cap.PID);
        printf("Lib1: Sent Target Address: 0x%lx\n", lib1_cap.PC);
        printf("Lib1: Sent MAC value: 0x%lx\n", lib1_cap.MAC);
        
        wait_for_call_via_loop();    

     }

    return 0;
}