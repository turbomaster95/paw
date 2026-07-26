OBJ := obj
SRC := src
TARGET := paw

CC := gcc
CFLAGS := -Iinclude -Wall -Wextra

Q = @

C_SRCS   := $(wildcard $(SRC)/*.c)
VM_SRCS  := $(wildcard $(SRC)/vm/*.c)
C_SRCS   += $(VM_SRCS)

GEN_SRCS := $(OBJ)/lex.yy.c

OBJS := $(patsubst $(SRC)/%.c, $(OBJ)/%.o, $(C_SRCS)) $(GEN_SRCS:.c=.o)
OBJS += $(OBJ)/libnu.a

all: setup $(OBJ)/libnu.a $(TARGET)

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

$(TARGET): $(OBJS) FORCE
	$(Q)echo "  LD      $@"
	$(Q)$(CC) $(CFLAGS) -o $@ $(OBJS)

CLEANF += $(OBJDIR)/libnu.a
DCLEAND += lib/libnu/build
DCLEANF += lib/libnu/configure
$(OBJ)/libnu.a:
	(cd lib/nu && ./compile && cp include/nu.h ../../include && cp include/nus.h ../../include && cp build/libnu.a ../../$(OBJ))

CLEANF += include/nu.h include/nus.h
include/nu.h: $(OBJ)/libnu.a

clean:
	$(Q)rm -rf $(OBJ) $(TARGET)
	$(Q)rm -rf $(CLEANF)
FORCE:

.PHONY: all setup clean FORCE
