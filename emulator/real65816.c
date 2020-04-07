#include <lib65816/cpu.h>
#include <lib65816/cpuevent.h>

#include <poll.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curses.h>

#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>

static const char *program = NULL;

static bool stdin_available(void) {
    struct pollfd pollfd = {
        .fd = STDIN_FILENO,
        .events = POLLIN,
        .revents = POLLIN,
    };
    return poll(&pollfd, 1, 0) == 0;
}

static void usage(int status) {
    FILE *stream = status == 0 ? stdout : stderr;
    fprintf(stream, "usage: %s [image]\n", program);
    exit(status);
}


/* we support all 16mb of memory */
#define MEMSIZE ((size_t)(1024 * 1024 * 16))

static byte ram[MEMSIZE];

#define UART1_ADDR ((uint32_t)0xFFF8)
#define UART2_ADDR ((uint32_t)0xFFF9)
#define EXIT_ADDR ((uint32_t)0xFFFF)

byte physical_read_io(word32 address, word32 timestamp, word32 emulFlags) {
    /* serial: putchar */
    if (address == UART2_ADDR) {
        return 0;
    }

    return 0;
}

byte physical_read(word32 address, word32 timestamp, word32 emulFlags) {
    if (address <= 0x00FFFF) {
        /* bank 0 */
        if ((address >= 0x00C000) && (address <= 0x00EFFF)){
            return physical_read_io(address, timestamp, emulFlags);
        } else if ((address >= 0x00F000) && (address <= 0x00FFFF)) {
            /* ROM */

            if (address == 0xFFFC) {
                return 0x00;
            } else if (address == 0xFFFD) {
                return 0x10;
            }
            return ram[address];
        }
    }

    assert(address <= MEMSIZE);
    return ram[address];
}

void physical_write_io(word32 address, byte value, word32 timestamp) {
}

void physical_write(word32 address, byte value, word32 timestamp) {
    /* serial: putchar */
    if (address == UART2_ADDR) {
        putchar(value);
        fflush(stdout);
        return;
    }

    /* set trace */
    if (address == 0x00cfff) {
        printf("CPU set trace = %#02x\n", value);
        CPU_setTrace(value);
        return;
    }

    /* terminate */
    if (address == EXIT_ADDR) {
        exit(value);
    }

    assert(address <= MEMSIZE);
    ram[address] = value;
}

void EMUL_handleWDM(byte opcode, word32 timestamp) {
    (void)timestamp;
    printf("%s(opcode = 0x%.2x)\n", __func__, opcode);
    exit(EXIT_FAILURE);
}

byte MEM_readMem(word32 address, word32 timestamp, word32 emulFlags) {
#if 0
    printf("%s(addres = %#6x)\n", __func__, address);
#endif
    address = address % MEMSIZE;

    return physical_read(address, timestamp, emulFlags);
}

void MEM_writeMem(word32 address, byte b, word32 timestamp) {
#if 0
    printf("%s(addres = %#6x)\n", __func__, address);
#endif
    address = address % MEMSIZE;
    physical_write(address, b, timestamp);
}

static void load_rom(const char *path) {
    FILE *file = NULL;

    if (!(file = fopen(path, "rb"))) {
        perror("fopen");
        abort();
    }

    size_t count = 0;
    size_t address = 0x001000;
    size_t max = 0x010000 - address;

    while ((count = fread(ram + address, 1, max, file)) > 0) {
        address += count;
        max -= count;
    }
    fclose(file);
}

static void endit(word32 timestamp) {
    printf("\nENDIT!\n");

    for (size_t i = 0; i < 32; i++) {
        const size_t addr = 0xffffe0 + i;
        printf("[%#x] = %#02x\n", addr, ram[addr]);
    }
    abort();
}

static struct CPUEvent endit_event = {
    .next = NULL,
    .previous = NULL,
    .counter = 0,
    .handler = &endit,
};

int main(int argc, char **argv) {
    program = argv[0];

    if (argc <= 1) {
        usage(EXIT_FAILURE);
    }

    load_rom(argv[1]);

    CPUEvent_initialize();
    CPUEvent_schedule(&endit_event, 10 * 10000, &endit);
    CPU_reset();
    if (argc >= 3) {
        CPU_setTrace(1);
    }
    CPU_run();

    for (size_t i = 0; i < 32; i++) {
        const size_t addr = 0xffe0 + i;
        printf("[%#x] = %#02x\n", addr, physical_read(addr, 0, 0));
    }

    return 0;
}
