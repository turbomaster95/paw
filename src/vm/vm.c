#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <vm.h>
#include <nu.h>

void vm_init(VM *vm) {
    vm->frame_count = 0;
    vm->stack_top = vm->stack;
}

void vm_free(VM *vm) {
    vm_init(vm);
}

void vm_push(VM *vm, Value value) {
    if (vm->stack_top - vm->stack >= STACK_MAX) {
        fprintf(stderr, "VM Stack Overflow\n");
        exit(EXIT_FAILURE);
    }
    *vm->stack_top++ = value;
}

Value vm_pop(VM *vm) {
    if (vm->stack_top == vm->stack) {
        fprintf(stderr, "VM Stack Underflow\n");
        exit(EXIT_FAILURE);
    }
    return *(--vm->stack_top);
}

static Value vm_peek(VM *vm, int distance) {
    return vm->stack_top[-1 - distance];
}

static void runtime_error(VM *vm, const char *format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);

    for (int i = vm->frame_count - 1; i >= 0; i--) {
        CallFrame *frame = &vm->frames[i];
        size_t instruction = frame->ip - frame->chunk->code - 1;
        fprintf(stderr, "[frame %d] offset %04ld in chunk\n", i, instruction);
    }

    vm_init(vm);
}

InterpretResult vm_run(VM *vm, Chunk *main_chunk) {
    CallFrame *frame = &vm->frames[vm->frame_count++];
    frame->chunk = main_chunk;
    frame->ip = main_chunk->code;
    frame->slots = vm->stack;

#define READ_BYTE() (*frame->ip++)
#define READ_SHORT() (frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
#define READ_CONSTANT() (frame->chunk->constants[READ_BYTE()])

    while (1) {
        uint8_t instruction = READ_BYTE();

        switch (instruction) {
            case OP_CONSTANT: {
                Value constant = READ_CONSTANT();
                vm_push(vm, constant);
                break;
            }

            case OP_ADD: {
                Value b = vm_pop(vm);
                Value a = vm_pop(vm);
                vm_push(vm, a + b);
                break;
            }

            case OP_SUB: {
                Value b = vm_pop(vm);
                Value a = vm_pop(vm);
                vm_push(vm, a - b);
                break;
            }

            case OP_MUL: {
                Value b = vm_pop(vm);
                Value a = vm_pop(vm);
                vm_push(vm, a * b);
                break;
            }

            case OP_DIV: {
                Value b = vm_pop(vm);
                Value a = vm_pop(vm);
                if (b == 0) {
                    runtime_error(vm, "Division by zero.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                vm_push(vm, a / b);
                break;
            }

            case OP_NEGATE: {
                vm_push(vm, -vm_pop(vm));
                break;
            }

            case OP_EQUAL: {
                Value b = vm_pop(vm);
                Value a = vm_pop(vm);
                vm_push(vm, a == b ? 1 : 0);
                break;
            }

            case OP_GREATER: {
                Value b = vm_pop(vm);
                Value a = vm_pop(vm);
                vm_push(vm, a > b ? 1 : 0);
                break;
            }

            case OP_LESS: {
                Value b = vm_pop(vm);
                Value a = vm_pop(vm);
                vm_push(vm, a < b ? 1 : 0);
                break;
            }

            case OP_POP: {
                vm_pop(vm);
                break;
            }

            case OP_GET_LOCAL: {
                uint8_t slot = READ_BYTE();
                vm_push(vm, frame->slots[slot]);
                break;
            }

            case OP_SET_LOCAL: {
                uint8_t slot = READ_BYTE();
                frame->slots[slot] = vm_peek(vm, 0);
                break;
            }

            case OP_JUMP: {
                uint16_t offset = READ_SHORT();
                frame->ip += offset;
                break;
            }

            case OP_JUMP_IF_FALSE: {
                uint16_t offset = READ_SHORT();
                if (vm_peek(vm, 0) == 0) {
                    frame->ip += offset;
                }
                break;
            }

            case OP_LOOP: {
                uint16_t offset = READ_SHORT();
                frame->ip -= offset;
                break;
            }

            case OP_CALL: {
                uint8_t arg_count = READ_BYTE();
                if (vm->frame_count == FRAMES_MAX) {
                    runtime_error(vm, "Stack overflow (too many call frames).");
                    return INTERPRET_RUNTIME_ERROR;
                }

                CallFrame *next_frame = &vm->frames[vm->frame_count++];
                next_frame->slots = vm->stack_top - arg_count;
                frame = next_frame;
                break;
            }

            case OP_RETURN: {
                Value result = vm_pop(vm);
                vm->frame_count--;

                if (vm->frame_count == 0) {
                    vm_pop(vm); /* Pop main frame */
                    vm_push(vm, result);
                    return INTERPRET_OK;
                }

                vm->stack_top = frame->slots;
                vm_push(vm, result);
                frame = &vm->frames[vm->frame_count - 1];
                break;
            }

            case OP_HALT:
                return INTERPRET_OK;

            default:
                runtime_error(vm, "Unknown opcode: 0x%02X", instruction);
                return INTERPRET_RUNTIME_ERROR;
        }
    }

#undef READ_BYTE
#undef READ_SHORT
#undef READ_CONSTANT
}
