#include <stdio.h>
#include <unistd.h>

#include <sys/syscall.h>
#include <sys/wait.h>

#include "cc-common.h"
int library_hosted_operand=250;

int library_add(void)      
{
   
    int a=0; 
    int b=0;
    int sum=0;
    asm volatile (
        ".word 0x02200120\n\t"  //cldg x9, [cr0] (operand_1) 
        ".word 0x02200141\n\t" //cldg x10, [cr1] (operand_2)
        //"mov x9, #4\n\t"
        //"mov x10, #9\n\t"
        "str x9, %0\n\t"          // Store x9 into memory at address of a
        "str x10, %1\n\t"         // Store x10 into memory at address of b
        :
        : "m" (a), "m" (b)   // Output operands for x9 and x10
        :"x9","x10"
    );

    printf("Operand 1:%d, Operand 2:%d\n", a,b);
    sum=a+b;
    sum+=library_hosted_operand;
    printf("Library returning to Application with value:%d\n", sum);
    syscall(SYS_pret, sum);
    exit(0);
    //wait_for_call_via_loop();    
}

int main(void)
{
    int pipe_fd[2];  // [0] for reading, [1] for writing

    // Create the pipe
    if (pipe(pipe_fd) == -1) {
        perror("pipe");
        exit(EXIT_FAILURE);
    }

    pid_t app_id = fork();
    
    if (app_id == 0) {

        // Application process
        close(pipe_fd[1]);  // Close the write end of the pipe

        // Execute the Caller-Data Owner program
        dup2(pipe_fd[0], STDIN_FILENO);  // Redirect pipe read end to stdin
        execl("./cc_application_data_owner", "cc_application_data_owner", NULL);
        // If execl fails
        perror("execl");
        exit(EXIT_FAILURE);

    } else {
        // library process
        close(pipe_fd[0]);  // close the read end of the pipe
        
        int lib_pid=getpid();
        
        
        // share the necessary info with the application process  
        pcap process_cap;
        process_cap.PC = (uint64_t)&library_add;  
        process_cap.PID = lib_pid;
        process_cap.MAC = 0;
     
        // Write the necessary callee info (PC, SP, and TTBR0) to the pipe
        write(pipe_fd[1], &process_cap, sizeof(pcap));

        printf("Library-Data Borrower: PID: %d\n", lib_pid);
        printf("Library-Data Borrower: Sent Target PID: %ld\n", process_cap.PID);
        printf("Library-Data Borrower: Sent Target Address: 0x%lx\n", process_cap.PC);
        printf("Library-Data Borrower: Sent MAC value: 0x%lx\n", process_cap.MAC);
        
        wait_for_call_via_loop();    

     }

    return 0;
}