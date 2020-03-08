.PHONY: all
all: libs my65816

include config.mk

INCLUDES := src/lib65816/cpu.h \
			src/lib65816/cpuevent.h

CPPFLAGS := -Isrc

%.c.o: %.c %.d
	$(CC) $(CCFLAGS) $(CPPFLAGS) -MD -MP -o $@ $<

LIB_SRCS := src/cpu.c \
	        src/cpuevent.c \
			src/debugger.c \
			src/dispatch.c \
			src/opcodes1.c \
			src/opcodes2.c \
			src/opcodes3.c \
			src/opcodes4.c \
			src/opcodes5.c \
			src/table.c
LIB_OBJS := $(LIB_SRCS:%=%.o)
LIB_DEPS := $(LIB_DEPS:%.c=%.d)

DEPS := $(LIB_DEPS) emulator/main.d

.PHONY: libs
libs: src/lib65816.a

src/lib65816.a: $(LIB_OBJS)
	ar r $@ $(LIB_OBJS)

.PHONY: clean
clean:
	$(DELETE) emulator/main.c.o
	$(DELETE) $(LIB_OBJS)
	$(DELETE) src/lib65816.a
	$(DELETE) $(DEPS)

.PHONY: install
install: install-my65816
	mkdir -p $(LIBPATH)
	cp src/lib65816.a $(LIBPATH)/

.PHONY: install-lib65816
	mkdir -p $(INCPATH)
	cp $(INCLUDES) $(INCPATH)

.PHONY: install-my65816

my65816: emulator/main.c.o src/lib65816.a
	$(LD) $(LDFLAGS) -o $@ $< src/lib65816.a

.PRECIOUS: %.d

%.d: ;

ifneq ($(MAKECMDGOALS),clean)
-include $(DEPS)
endif
