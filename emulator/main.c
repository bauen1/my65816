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

/* MMU:
 *  the mmu has 1024 entries, indexed by the upper 10 bits of the address line
 *  each entry is 2 bytes big
 *  layout of each entry:
 *    0 - 10: physical
 *    11:
 *    12:
 *    13: user
 *    14: write
 *    15: present
 *
 *
 * MMU [0x00E000 - 0x00EFFF]
 * [0x000 - 0x000]: control
 *   [0x000]: control register:
 *   bit 0: enable
 *   bit 1: allow code execution from read-write segment
 *   [0x001]: mode
 *   bit 0: supervisor
 * [0xc00 - 0xeff]: entries
 * */

/* physical memory map:
 *  [0x000000 - 0x00AFFF]: RAM
 *  [0x00C000 - 0x00EFFF]: IO
 *  [0x00F000 - 0x00FFFF]: Boot ROM
 *  [0x010000 - 0xFFFFFF]: RAM (optional)
 * */

/* IO map [0x00C000 - 0x00EFFF]:
 * [0x00C000 - 0x00CFFF]: serial
 * [0x00E000 - 0x00EFFF]: MMU
 * */

#define MMU_ENTRIES ((size_t)1024)
static word16 mmu_entries[MMU_ENTRIES];
static bool mmu_enabled = false;
static bool mmu_supervisor = true;
static bool mmu_shadow_upper_bank0 = false;
static bool mmu_rti_to_user = false;

#define MMU_ENTRY_PRESENT ((uint16_t)(0x8000))
#define MMU_ENTRY_WRITE   ((uint16_t)(0x4000))
#define MMU_ENTRY_USER    ((uint16_t)(0x2000))

/* we support all 16mb of memory */
#define MEMSIZE ((size_t)(1024 * 1024 * 16))

static byte ram[MEMSIZE];

byte physical_read_io(word32 address, word32 timestamp, word32 emulFlags) {
    /* serial: putchar */
    if (address == 0x00c0d0) {
        return 0;
    }

    /* serial: test input */
    if (address == 0x00c0d1) {
        /* return 1 if input is available */
        return stdin_available() ? 1 : 0;
    }

    /* serial: read non-blocking */
    if (address == 0x00C0D3) {
        if (stdin_available()) {
            return getchar();
        } else {
            return 0x00;
        }
    }

    /* mmu: control register 1 */
    if (address == 0x00E000) {
        return mmu_enabled ? 1 : 0;
    }

    /* mmu: control register 2 */
    if (address == 0x00E001) {
        return mmu_supervisor ? 1 : 0;
    }

    /* mmu: control register 3 */
    if (address == 0x00E002) {
        return mmu_shadow_upper_bank0 ? 1 : 0;
    }

    /* mmu: control register 4 */
    if (address == 0x00E003) {
        return mmu_rti_to_user ? 1 : 0;
    }

    /* mmu: entries */
    if ((address >= 0x00E800) && (address <= 0x00EFFF)) {
        size_t index = address - 0x00E800;
        index /= 2;
        assert(index <= MMU_ENTRIES);

        word32 entry = mmu_entries[index];

        if (address % 2 == 0) {
            return entry & 0xFF;
        } else {
            return entry >> 8;
        }
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
            return ram[address];
        }
    }

    assert(address <= MEMSIZE);
    return ram[address];
}

void physical_write_io(word32 address, byte value, word32 timestamp) {
    /* serial: putchar */
    if (address == 0x00c0d0) {
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

    /* mmu: control register 1 */
    if (address == 0x00E000) {
        mmu_enabled = (value & 0x1) == 1 ? true : false;
        return;
    }

    /* mmu: control register 2 */
    if (address == 0x00E001) {
        mmu_supervisor = (value & 0x1) == 1 ? true : false;
        return;
    }

    /* mmu: control register 3 */
    if (address == 0x00E002) {
        mmu_shadow_upper_bank0 = (value & 0x1) == 1 ? true : false;
        return;
    }

    /* mmu: control register 4 */
    if (address == 0x00E003) {
        mmu_rti_to_user = (value & 0x1) == 1 ? true : false;
        return;
    }

    /* mmu: entries */
    if ((address >= 0x00e800) && (address <= 0x00efff)) {
        size_t index = address - 0xe800;
        index /= 2;
        assert(index <= MMU_ENTRIES);
        word32 entry = mmu_entries[index];

        if (address % 2 == 0) {
            mmu_entries[index] = (entry & 0xFF00) | (value & 0xFF);
        } else {
            mmu_entries[index] = (value << 8) | (entry & 0x00FF);
        }
        return;
    }
}

void physical_write(word32 address, byte value, word32 timestamp) {
    if (address <= 0x00FFFF) {
        /* bank 0 */

        if ((address >= 0x00C000) && (address <= 0x00EFFF)) {
            /* IO */
            return physical_write_io(address, value, timestamp);
        } else if ((address >= 0x00F000) && (address <= 0x00FFFF)) {
            /* ROM */
            return;
        }
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
    address = address % MEMSIZE;

    byte v;
    if (mmu_enabled) {
        if (emulFlags & EMUL_PIN_VP) {
            /* vector fetch */
            mmu_supervisor = true;
        }

        size_t mmu_index = (address >> 14) & 0x7FF;

        if (mmu_supervisor) {
            if ((emulFlags & EMUL_PIN_VPA) || (emulFlags & EMUL_PIN_VP)) {
                /* shadow upper 16kb of bank 0 */
                if ((mmu_shadow_upper_bank0) && (mmu_index == 3)) {
                    mmu_index = 0x3ff;
                }
            }
        }

        word16 entry = mmu_entries[mmu_index];

        if ((entry & MMU_ENTRY_PRESENT) == 0) {
            printf("read: not present!\n");
            CPU_abort();
            return 0;
        }

        if (entry & MMU_ENTRY_USER) {
            if (emulFlags & EMUL_PIN_VPA) {
                mmu_supervisor = false;
            }
        }

        if (!mmu_supervisor) {
            if ((entry & MMU_ENTRY_USER) == 0) {
                printf("read: user access to non-user entry!\n");
                CPU_abort();
            }
        }

        word32 physical_address = 0;
        physical_address = ((entry & 0x7FF) << 14) | (address & 0x3FFF);

        v = physical_read(physical_address, timestamp, emulFlags);

        if (emulFlags & EMUL_PIN_VPA) {
            if (mmu_supervisor) {
                if (v == 0x40) { /* RTI */
                    if (mmu_rti_to_user) {
                        mmu_supervisor = false;
                    }
                }
            } else {
                /* prevent: SEI, STP, WAI, XCE instructions */
                if (v == 0x78)  {
                    /* SEI */
                    CPU_abort();
                    return 0xEA; /* NOP */
                } else if (v == 0xDB) {
                    /* STP */
                    CPU_abort();
                    return 0xEA; /* NOP */
                } else if (v == 0xCB) {
                    /* WAI */
                    CPU_abort();
                    return 0xEA; /* NOP */
                } else if (v == 0xFB) {
                    /* XCE */
                    /* FIXME: we might actually allow this */
                    CPU_abort();
                    return 0xEA; /* NOP */
                }

            }
        }
    } else {
        v = physical_read(address, timestamp, emulFlags);
    }
    return v;
}

void MEM_writeMem(word32 address, byte b, word32 timestamp) {
    address = address % MEMSIZE;

    if (mmu_enabled) {
        size_t mmu_index = (address >> 14) & 0x7FF;

        word16 entry = mmu_entries[mmu_index];

        if ((entry & MMU_ENTRY_WRITE) == 0) {
            printf("write: read-only entry!\n");
            CPU_abort();
            return;
        }

        if (!mmu_supervisor) {
            if ((entry & MMU_ENTRY_USER) == 0) {
                printf("write: user access to non-user entry!\n");
                CPU_abort();
                return;
            }
        }

        const word32 physical_address = ((entry & 0x7FF) << 14) | (address & 0x3FFF);
        physical_write(physical_address, b, timestamp);
    } else {
        physical_write(address, b, timestamp);
    }
}

static void load_rom(const char *path) {
    FILE *file = NULL;

    if (!(file = fopen(path, "rb"))) {
        perror("fopen");
        abort();
    }

    size_t count = 0;
    size_t address = 0x00F000;
    size_t max = 0x010000 - address;

    while ((count = fread(ram + address, 1, max, file)) > 0) {
        address += count;
        max -= count;
    }
    fclose(file);
}

static void endit(word32 timestamp) {
    printf("\nENDIT!\n");
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
    CPUEvent_schedule(&endit_event, 16 * 1024, &endit);
    CPU_reset();
    // CPU_setTrace(1);
    CPU_run();

    return 0;
}
