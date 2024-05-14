rm -f payload.elf payload.bin payload_bin.c patcher patcher.exe

$DEVKITARM/bin/arm-none-eabi-gcc \
-mcpu=arm7tdmi -nostartfiles -nodefaultlibs -mthumb -fPIE -Os -fno-toplevel-reorder \
payload.c -T payload.ld -o payload.elf
$DEVKITARM/bin/arm-none-eabi-objcopy -O binary payload.elf payload.bin
xxd -i payload.bin > payload_bin.c

# Linux bin
gcc -g patcher.c payload_bin.c -o patcher
# Windows exe
i686-w64-mingw32-gcc -g patcher.c payload_bin.c -o patcher