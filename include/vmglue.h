#ifndef VMGLUE_H
#define VMGLUE_H

#include <stdint.h>

// Unsigned Integers
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

// Signed Integers
typedef int8_t   i8;
typedef int16_t  i16;
typedef int32_t  i32;
typedef int64_t  i64;

#define MAX_REGS 16
#define MAX_STACK_SIZE 512

#define ROM_SIZE 2048
#define RAM_SIZE 65536

#define VM_MAGIC 0x56574150
#define VM_VERSION 1

// CPU Flags
#define FLAG_ZERO     (1 << 0)
#define FLAG_NEGATIVE (1 << 1)
#define FLAG_CARRY    (1 << 2)
#define FLAG_OVERFLOW (1 << 3)

#endif // VMGLUE_H
