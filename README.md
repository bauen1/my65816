# Simulator for my65816

# MMU

The lower 14 bits of the 65816 address line go directly into the memory decoder.
The upper 10 bits of the 65816 address line feed into the MMU.
The MMU supplies the upper 10+ bits for the memory decoder.
The MMU is connected to the data bus (maybe even between processor and memory).

The MMU has 1024 entries (TODO: we could multiply this easily and allow switching using a register),these are indexed by the upper 10 address bits

On reset the MMU is disabled

entry (2 bytes):
0 - 10: physical address
11: (additional physical)
12: (no-execute ?) (additional physical)
13: user
14: write
15: present

```pseudo
enabled: 1 = false
mode: user or supervisor = supervisor
shadow_upper: 1 = true
mmu_entries[2^10]

// reason for abort
reason: entry_not_present or illegal_operation or write_to_read_only or user_execute_non_user

rti_to_user = false

on read (virt_index: 10, type: instruction_fetch or read or write or vector_fetch):
  if enabled:
    if type == vector_fetch:
      mode = supervisor

    if mode == supervisor and shadow_upper:
      if type == instruction_fetch:
        // by only redirecting instruction fetches, we can allow supervisor code execution from
        // this range, but also allow data access (stack)
        if virt_index == 3: // 3  -> 0x00c000 - 0x00ffff
          virt_index = -1   // -1 -> 0xffc000 - 0xffffff

    entry = mmu_entries[virt_index]

    if not entry.present:
      reason = entry_not_present
      ABORT

    if entry.user:
      mode = user

    if mode == user:
      if not entry.user:
        reason = user_execute_non_user
        ABORT

    if type == write:
      if not entry.write:
        // FIXME: couldn't this cause a loop by having the stack point somewhere invalid
        // and causing an interrupt, which switches to supervisor and then tries to push
        // FIXME: look at docs of 65816 if vector fetch is before or after vector fetch
        reason = write_to_read_only
        ABORT

    data = memory_decoder_read(entry.phys)

    if type == instruction_fetch:
      if mode == user:
        if data == SEI or data == STP or data == WAI or data == XCE:
          reason = illegal_operation
          ABORT
      else:
        if data == RTI:
          if rti_to_user:
            mode = user
```
