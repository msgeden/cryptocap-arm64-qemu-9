#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>

#define SYS_ccall 464  // Adjust syscall numbers as needed
#define SYS_cret 465
#define SYS_pcall 466  // Adjust syscall numbers as needed
#define SYS_pret 467
#define SYS_dcall 468  // Adjust syscall numbers as needed
#define SYS_dret 469
#define SYS_acall 470  // Adjust syscall numbers as needed
#define SYS_aret 471
#define entry_func 0x400000


typedef struct dcap {
     uint64_t perms_base;
     uint32_t offset;
     uint32_t size;
     uint64_t PT;
     uint64_t MAC;
} dcap;

typedef struct pcap {
     uint64_t PID;
     uint64_t PC;
     uint64_t MAC;
} pcap;

uint64_t getPTBase() {
    uint64_t value=0xDEADBEEF;
    asm volatile(".word 0x03300000" : "=r"(value));  //readttbr x0
    //printf("get_ttbr0_el1(): 0x%lx\n", value);
    return value;
}

void wait_for_call_via_loop(void){
    while(1) {
        sleep(1);  // Wait for calls
    }
}