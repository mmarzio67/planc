CC      := cc
CSTD    := -std=c17
CFLAGS  := $(CSTD) -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude
LDFLAGS :=

SRC := \
	src/main.c \
	src/cli.c \
	src/plan.c \
	src/storage.c \
	src/util.c

OBJ := $(SRC:.c=.o)
BIN := plan

.PHONY: all clean run

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

run: $(BIN)
	./$(BIN) list

clean:
	rm -f $(OBJ) $(BIN)