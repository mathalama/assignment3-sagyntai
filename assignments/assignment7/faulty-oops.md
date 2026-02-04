# Faulty Kernel Oops Analysis

## Instructions
1. Run the qemu image.
2. Execute `echo "hello_world" > /dev/faulty`.
3. Capture the kernel oops output from the console or `/var/log/messages`.

## Expected Result
The operation should trigger a kernel panic or oops due to a NULL pointer dereference.

## Analysis Steps
1. Look at the "IP:" line in the oops message. It shows the address of the crashing instruction.
   Example: `IP: [<ffffffffa0000000>] faulty_write+0x10/0x20 [faulty]`
2. The `+0x10` indicates the offset from the beginning of the function.
3. Disassemble the `faulty.ko` module:
   `objdump -d faulty.ko`
4. Find the `faulty_write` function and look for the instruction at offset `0x10`.
5. It should correspond to the C code line `*(int *)0 = 0;` which writes to a NULL pointer.
