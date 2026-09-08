OBJ := obj
SRC := src
TARGETC := pawc
TARGETV := pawv

WASI_OBJ := obj-wasi
WASI_CC  := clang
WASI_TARGET_FLAG := --target=wasm32-wasip1 -D_WASI_EMULATED_PROCESS_CLOCKS -DPK_ENABLE_SOCKET=0 -DPK_ENABLE_OS=0
WASI_TARGETC := $(TARGETC).wasm
WASI_TARGETV := $(TARGETV).wasm

CC := gcc
CFLAGS := -Iinclude -Wall -Wextra -pedantic -Wno-unused -Ilib -MMD -MP -MF $(OBJ)/$@.d
COPTS :=

CFLAGS += $(COPTS)

Q = @

NU_BUILD_DIR      := lib/nu/build
NU_OBJ_A          := $(OBJ)/libnu.a

WASI_NU_BUILD_DIR := lib/nu/build-wasi
WASI_NU_OBJ_A     := $(WASI_OBJ)/libnu.a

EXCLUDE  := $(SRC)/prep.c
C_SRCS   := $(filter-out $(EXCLUDE), $(wildcard $(SRC)/*.c))

VM_SRCS  := $(wildcard $(SRC)/vm/*.c)

GEN_SRCS := $(OBJ)/lex.yy.c

OBJS := $(patsubst $(SRC)/%.c, $(OBJ)/%.o, $(C_SRCS)) $(GEN_SRCS:.c=.o)
OBJS += $(OBJ)/libnu.a

VM_OBJS := $(patsubst $(SRC)/vm/%.c, $(OBJ)/vm/%.o, $(VM_SRCS))
VM_OBJS += $(OBJ)/libnu.a

WASI_OBJS := $(patsubst $(SRC)/%.c, $(WASI_OBJ)/%.o, $(C_SRCS)) $(WASI_OBJ)/lex.yy.c
WASI_OBJS := $(WASI_OBJS:.c=.o) $(WASI_OBJ)/libnu.a

WASI_VM_OBJS := $(patsubst $(SRC)/vm/%.c, $(WASI_OBJ)/vm/%.o, $(VM_SRCS))
WASI_VM_OBJS += $(WASI_OBJ)/libnu.a

all: setup $(OBJ)/libnu.a $(TARGETC) $(TARGETV)

setup:
	$(Q)mkdir -p $(OBJ) $(OBJ)/vm

$(OBJ)/lex.yy.c: $(SRC)/lex.l setup
	$(Q)echo "  FLEX    $<"
	$(Q)flex -o $@ $<

$(OBJ)/%.o: $(SRC)/%.c setup
	$(Q)echo "  CC      $<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@

$(OBJ)/%.o: $(GEN_SRCS) setup
	$(Q)echo "  CC      $<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@

$(TARGETC): $(OBJS) FORCE $(SRC)/prep.c
	$(Q)echo "  LD      $@"
	$(Q)$(CC) $(CFLAGS) -o $@ $(OBJS)

$(TARGETV): $(VM_OBJS) FORCE
	$(Q)echo "  LD      $@"
	$(Q)$(CC) $(CFLAGS) -o $@ $(VM_OBJS) $(LDFLAGS)

$(NU_OBJ_A): include/nu.h include/nus.h setup
	$(Q)echo "  CMAKE   lib/nu (Native)"
	$(Q)cmake -B $(NU_BUILD_DIR) -S lib/nu
	$(Q)cmake --build $(NU_BUILD_DIR)
	$(Q)cp $(NU_BUILD_DIR)/libnu.a $@

CLEANF += include/nu.h
include/nu.h:
	$(Q)(cd lib/nu && cp include/nu.h ../../include 2>/dev/null || cp nu.h ../../include)

CLEANF += include/nus.h
include/nus.h:
	$(Q)(cd lib/nu && cp include/nus.h ../../include 2>/dev/null || cp nus.h ../../include)

wasi-setup: include/nu.h include/nus.h
	$(Q)mkdir -p $(OBJ) $(OBJ)/vm
	$(Q)mkdir -p $(WASI_OBJ) $(WASI_OBJ)/vm

$(WASI_OBJ)/lex.yy.c: $(OBJ)/lex.yy.c wasi-setup
	$(Q)cp $< $@

$(WASI_OBJ)/%.o: $(SRC)/%.c wasi-setup
	$(Q)echo "  WASI CC $<"
	$(Q)$(WASI_CC) $(WASI_TARGET_FLAG) $(CFLAGS) -c $< -o $@

$(WASI_OBJ)/%.o: $(WASI_OBJ)/%.c wasi-setup
	$(Q)echo "  WASI CC $<"
	$(Q)$(WASI_CC) $(WASI_TARGET_FLAG) $(CFLAGS) -c $< -o $@

$(WASI_NU_OBJ_A): include/nu.h include/nus.h wasi-setup
	$(Q)echo "  CMAKE   lib/nu (WASI)"
	$(Q)cmake -B $(WASI_NU_BUILD_DIR) -S lib/nu \
		-DCMAKE_C_COMPILER=$(WASI_CC) \
		-DCMAKE_C_FLAGS="$(WASI_TARGET_FLAG)" \
		-DCMAKE_SYSTEM_NAME=Generic
	$(Q)cmake --build $(WASI_NU_BUILD_DIR)
	$(Q)cp $(WASI_NU_BUILD_DIR)/libnu.a $@

$(WASI_TARGETC): $(WASI_OBJS) FORCE $(SRC)/prep.c
	$(Q)echo "  WASI LD $@"
	$(Q)$(WASI_CC) $(WASI_TARGET_FLAG) $(CFLAGS) -o $@ $(WASI_OBJS)

$(WASI_TARGETV): $(WASI_VM_OBJS) FORCE
	$(Q)echo "  WASI LD $@"
	$(Q)$(WASI_CC) $(WASI_TARGET_FLAG) $(CFLAGS) -o $@ $(WASI_VM_OBJS) $(LDFLAGS)

wasi: wasi-setup $(OBJ)/lex.yy.c $(WASI_NU_OBJ_A) $(WASI_TARGETC) $(WASI_TARGETV)

clean:
	rm -rf $(TARGETC) $(TARGETV) $(WASI_TARGETC) $(WASI_TARGETV)
	rm -rf $(CLEANF) $(OBJ) $(WASI_OBJ)
	rm -rf $(NU_BUILD_DIR) $(WASI_NU_BUILD_DIR)

FORCE:

.PHONY: all setup clean FORCE wasi wasi-setup
