OBJ := obj
SRC := src
TARGETC := pawc
TARGETV := pawv

WASI_OBJ := obj-wasi
WASI_CC  := clang
WASI_TARGET_FLAG := --target=wasm32-wasip1
WASI_TARGETC := $(TARGETC).wasm
WASI_TARGETV := $(TARGETV).wasm

CC := gcc
CFLAGS := -Iinclude -Wall -Wextra -pedantic -Wno-unused -Ilib
COPTS :=

CFLAGS += $(COPTS)

Q = @

NU_BUILD_A := lib/nu/build/libnu.a
NU_OBJ_A   := $(OBJ)/libnu.a
WASI_NU_OBJ_A := $(WASI_OBJ)/libnu.a

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
	$(Q)mkdir -p $(OBJ)
	$(Q)mkdir -p $(OBJ)/vm

$(OBJ)/lex.yy.c: $(SRC)/lex.l
	$(Q)echo "  FLEX    $^"
	$(Q)flex -o $@ $^

$(OBJ)/%.o: $(SRC)/%.c
	$(Q)echo "  CC      $<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@

$(OBJ)/%.o: $(GEN_SRCS)
	$(Q)echo "  CC      $<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@

$(TARGETC): $(OBJS) FORCE $(SRC)/prep.c
	$(Q)echo "  LD      $@"
	$(Q)$(CC) $(CFLAGS) -o $@ $(OBJS)

$(TARGETV): $(VM_OBJS) FORCE
	$(Q)echo "  LD      $@"
	$(Q)$(CC) $(CFLAGS) -o $@ $(VM_OBJS) $(LDFLAGS)

$(NU_OBJ_A): include/nu.h include/nus.h
	$(Q)mkdir -p $(OBJ)
	$(Q)if [ -f $(NU_BUILD_A) ]; then \
		echo "  NU      $(NU_BUILD_A) -> $@"; \
		cp $(NU_BUILD_A) $@; \
	else \
		echo "  NU         (missing $(NU_BUILD_A))"; \
		(cd lib/nu && ./compile && cp build/libnu.a ../../$(NU_OBJ_A)); \
	fi

CLEANF += include/nu.h
include/nu.h:
	$(Q)(cd lib/nu && cp include/nu.h ../../include)

CLEANF += include/nus.h
include/nus.h:
	$(Q)(cd lib/nu && cp include/nus.h ../../include)

wasi-setup: include/nu.h include/nus.h
	$(Q)mkdir -p $(WASI_OBJ)
	$(Q)mkdir -p $(WASI_OBJ)/vm

$(WASI_OBJ)/lex.yy.c: $(OBJ)/lex.yy.c wasi-setup
	$(Q)cp $< $@

$(WASI_OBJ)/%.o: $(SRC)/%.c wasi-setup
	$(Q)echo "  WASI CC $<"
	$(Q)$(WASI_CC) $(WASI_TARGET_FLAG) $(CFLAGS) -c $< -o $@

$(WASI_OBJ)/%.o: $(WASI_OBJ)/%.c wasi-setup
	$(Q)echo "  WASI CC $<"
	$(Q)$(WASI_CC) $(WASI_TARGET_FLAG) $(CFLAGS) -c $< -o $@

$(WASI_NU_OBJ_A): include/nu.h include/nus.h wasi-setup
	$(Q)echo "  WASI NU libnu.a"
	$(Q)(cd lib/nu && CC="$(WASI_CC) $(WASI_TARGET_FLAG)" ./compile && cp build/libnu.a ../../$(WASI_NU_OBJ_A))

$(WASI_TARGETC): $(WASI_OBJS) FORCE $(SRC)/prep.c
	$(Q)echo "  WASI LD @"
	$(Q)$(WASI_CC) $(WASI_TARGET_FLAG) $(CFLAGS) -o $@ $(WASI_OBJS)

$(WASI_TARGETV): $(WASI_VM_OBJS) FORCE
	$(Q)echo "  WASI LD @"
	$(Q)$(WASI_CC) $(WASI_TARGET_FLAG) $(CFLAGS) -o $@ $(WASI_VM_OBJS) $(LDFLAGS)

wasi: wasi-setup $(OBJ)/lex.yy.c $(WASI_NU_OBJ_A) $(WASI_TARGETC) $(WASI_TARGETV)

clean:
	rm -rf $(TARGETC) $(TARGETV) $(WASI_TARGETC) $(WASI_TARGETV)
	rm -rf $(CLEANF) $(OBJ) $(WASI_OBJ)

FORCE:

.PHONY: all setup clean FORCE wasi wasi-setup
