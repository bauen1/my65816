LIB65816_A ?= ../prefix/lib/lib65816.a
LIB65816_INCLUDE ?= ../prefix/include

CFLAGS = -Wall -Wextra

CPPFLAGS = -I $(LIB65816_INCLUDE) -I $(LIB65816_INCLUDE)/lib65816

all: my65816
.PHONY: all clean

clean:
	rm -f my65816

my65816: main.c $(LIB65816_A) $(LIB65816_INCLUDE)
	$(CC) $(CFLAGS) $(CPPFLAGS) $< -o $@ $(LIB65816_A)
