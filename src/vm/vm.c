#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <vm.h>

int32_t run_paw_vm(VM *vm, Memory *mem, size_t prog_len) {
    if (!vm || !mem) return -1;

    return VM_run(vm, mem->rom, prog_len, mem);
}

