OBJ := obj
SRC := src
TARGETC := pawc
TARGETV := pawv

CC := gcc
CFLAGS := -Iinclude -Wall -Wextra -pedantic -Wno-unused
COPTS := 

CFLAGS += $(COPTS)

Q = @

NU_BUILD_A := lib/nu/build/libnu.a
NU_OBJ_A   := $(OBJ)/libnu.a

EXCLUDE  := $(SRC)/prep.c
C_SRCS   := $(filter-out $(EXCLUDE), $(wildcard $(SRC)/*.c))

VM_SRCS  := $(wildcard $(SRC)/vm/*.c)

GEN_SRCS := $(OBJ)/lex.yy.c

OBJS := $(patsubst $(SRC)/%.c, $(OBJ)/%.o, $(C_SRCS)) $(GEN_SRCS:.c=.o)
OBJS += $(OBJ)/libnu.a

VM_OBJS := $(patsubst $(SRC)/vm/%.c, $(OBJ)/vm/%.o, $(VM_SRCS))
VM_OBJS += $(OBJ)/libnu.a

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

$(TARGETC): $(OBJS) FORCE
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

CLEANF += $(OBJS)
clean:
	rm -rf $(TARGET)
	rm -rf $(CLEANF)

FORCE:

.PHONY: all setup clean FORCE
