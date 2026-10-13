# M0 boot gate (not yet executed)

For each assembled Blade and Computer board:

1. Record board/module markings, flash capacity, USB/UART path, power source, and
   firmware commit.
2. Build and upload only the matching target and serial port.
3. Capture the complete 115200 baud boot log.
4. Confirm the target-specific `M0 ready` line and absence of reset loops.
5. Record image size and minimum free heap from an approved measurement build.

Compilation alone does not pass this gate.
