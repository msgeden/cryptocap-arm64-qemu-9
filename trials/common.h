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
    //printf("get_ttbr0_el1(): 0x%lx\n", value);
    return value;
}

uint64_t getKernelPTBase() {
    uint64_t value=0xDEADBEEF;
    asm volatile(".word 0x03300020" : "=r"(value));  //readttbr x0
    //printf("get_ttbr1_el1(): 0x%lx\n", value);
    return value;
}

uint64_t getKernelSP() {
    uint64_t value=0xDEADBEEF;
    asm volatile(".word 0x03c00000" : "=r"(value));  //readspel1 x0
    //printf("get_sp_el1(): 0x%lx\n", value);
    return value;
}