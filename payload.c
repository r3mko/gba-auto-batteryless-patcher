#define AGB_ROM  ((unsigned char*)0x8000000)
#define AGB_SRAM ((volatile unsigned char*)0xE000000)
#define SRAM_SIZE 64
#define AGB_SRAM_SIZE SRAM_SIZE*1024
#define SRAM_BANK_SEL (*(volatile unsigned short*) 0x09000000)
#define _FLASH_WRITE(pa, pd) { *(((unsigned short *)AGB_ROM)+((pa)/2)) = pd; __asm("nop"); }

asm(R"(

.text
original_entrypoint:
    .word 0x080000c0

flush_mode:
    .word 0

save_size:
    .word 0x20000
    .word patched_entrypoint
    .word write_sram_patched + 1
    .word write_flash_patched + 1
    .word write_eeprom_patched + 1
    .word write_eeprom_v111_posthook + 1

.arm
patched_entrypoint:
    mov r0, #0x04000000

    # Install irq handler based on mode
    ldr r1, flush_mode
    cmp r1, #0
    adr r1, idle_irq_handler
    adrne r1, keypad_irq_handler
    str r1, [r0, #-4]

    # Set save sector and size
    adrl r0, flash_save_sector
    mov r1, #0x0e000000
    ldr r2, save_size
    add r2, r1
    mov r3, #0x09000000

    # Lock 369in1 mapper
    mov r4, #0x80
    strb r4, [r1, #3]

sram_init_loop:
    # Write byte to sram
    lsr r4, r1, #16
    and r4, #1
    strh r4, [r3]
    nop
    ldrb r4, [r0], #1
    strb r4, [r1], #1
    cmp r1, r2
    blo sram_init_loop

    # Set bank to 0 for banking-unaware software
    mov r4, #0
    strh r4, [r3]

    # Jump to original entrypoint
    ldr pc, original_entrypoint

.thumb
# r0 = src, r1 = dst, r2 = size. Check if change before writing, only install irq if change

.type write_sram_patched, %function
write_sram_patched:
    # Save return address and register states
    push {lr}
    push {r4, r5, r6, r7}

    # Disable interrupts while writing - just in case
    ldr r6, =0x04000208
    ldrh r7, [r6]
    mov r3, #0
    strh r3, [r6]
    
    # Writes will never span both SRAM banks - so only needed to write once.
    mov r4, #0x09
    lsl r4, #24
    lsr r5, r1, #16
    mov r3, #1
    and r5, r3
    strh r5, [r4]

    # Set size
    add r2, r0

write_sram_patched_loop:
    # Check if the each byte to write to sram is different - if it is, write it, then set a flag
    ldrb r4, [r0]
    ldrb r5, [r1]
    cmp r4, r5
    beq (.+6)
    mov r3, #1
    strb r4, [r1]
    add r0, #1
    add r1, #1
    cmp r0, r2
    blo write_sram_patched_loop

    # If the flag was not set, the function had no effect. Short circuit
    cmp r3, #0
    beq write_sram_patched_exit

    # Install the chosen irq handler and initialise countdown value if needed.
    mov r0, pc
    sub r0, #.+2-flush_mode
    ldrh r0, [r0]
    cmp r0, #0
    bne write_sram_patched_exit
    
    bl install_countdown_handler

write_sram_patched_exit:
    # Restore interrupts and registers
    strh r7, [r6]
    mov r0, #0
    pop {r4, r5, r6, r7}
    pop {r1}
    bx r1

# r0 = sector number, r1 = source data 0x1000 bytes

.type write_flash_patched, %function
write_flash_patched:
    # Get data (4096 byte sectors) from flash
    lsl r0, #12
    mov r2, #0x0e
    lsl r2, #24
    orr r0, r2
    mov r2, #0x1
    lsl r2, #12
    mov r3, r0
    mov r0, r1
    mov r1, r3
    
    b write_sram_patched

.ltorg

# r0 = eeprom address, r1 = src data (needs byte swapping, 8 bytes)

.type write_eeprom_patched, %function
write_eeprom_patched:
    # Save states and init
    push {r4, lr}
    mov r2, r1
    add r2, #8
    mov r3, sp

write_eeprom_patched_byte_swap_loop:
    # Byte-swap 8 bytes
    ldrb r4, [r1]
    add r1, #1
    sub r3, #1
    strb r4, [r3]
    cmp r1, r2
    bne write_eeprom_patched_byte_swap_loop

    # Address translation
    mov r1, #0x0e
    lsl r1, #24
    lsl r0, #3
    add r1, r0
    mov r2, #8
    mov r0, r3
    mov sp, r3
    bl write_sram_patched

    # Cleanup and restore
    add sp, #8
    pop {r4, pc}

.type write_eeprom_patched, %function
write_eeprom_v111_posthook:
    push {r0}
    bl install_countdown_handler
    pop {r0}
    bx r0
    
install_countdown_handler:
    # Load and set the address and timer value into memory 
    adr r0, countdown_irq_handler
    mov r1, #0x04
    lsl r1, #24
    sub r1, #0x10
    mov r2, #102
    strh r2, [r1, #0x0a]
    str r0, [r1, #0x0c]

    # Debug: set green swap as a visual indicator that the countdown has begun
    #strh r2, [r1, #0x12]

    bx lr

.arm
# IRQ handlers are called with 0x04000000 in r0 which is handy!

keypad_irq_handler:
    # Check keypad register for L+R+START+SELECT
    # May need to be changed to ldrh
    ldr r3, [r0, #0x130]
    teq r3, #0xf3
    ldrne pc, [r0, #-12]
    
    # Debug: enable green swap
    #mov r1, #1
    #strh r1, [r0, #2]
    
    # Switch to system mode to get lots of stack
    mov r3, #0x9f
    msr cpsr, r3

    # Save link register, flush sram, restore link register
    push {lr}
    bl flush_sram
    pop {lr}

    # Return to irq mode
    mov r3, #0x92
    msr cpsr, r3
    
    # Debug: disable green swap
    #mov r0, #0x04000000
    #strh r0, [r0, #2]

    # Save sound state then disable it
    mov r0, #0x04000000
    ldrh r2, [r0, #0x0080]
    ldrh r3, [r0, #0x0084]
    push {r2, r3}
    strh r0, [r0, #0x0084]

    # Wait until keypad register is no longer L+R+START+SELECT
    ldr r3, [r0, #0x130]
    teq r3, #0xf3
    beq (.-8)

    # Restore sound state
    pop {r2, r3}
    strh r2, [r0, #0x0080]
    strh r3, [r0, #0x0084]

    # Idle irq handler
    ldr pc, [r0, #-12]

countdown_irq_handler:
    # If not vblank IF then continue
    ldr r1, [r0, #0x200]
    tst r1, #0x00010000
    ldreq pc, [r0, #-12]

    # If countdown hits 0 then contiue
    ldrh r1, [r0, #-6]
    subs r1, #1
    strh r1, [r0, #-6]
    ldrne pc, [r0, #-12]

    # Switch to system mode to get lots of stack
    mov r3, #0x9f
    msr cpsr, r3

    # Save link register, flush sram, restore link register
    push {lr}
    bl flush_sram
    pop {lr}
    
    # Return to irq mode
    mov r3, #0x92
    msr cpsr, r3

    # Debug: disable green swap
    #mov r0, #0x04000000
    #strh r0, [r0, #0x02]
    
    # Uninstall countdown irq handler 
    mov r0, #0x04000000
    adr r1, idle_irq_handler
    str r1, [r0, #-4]
    
    # Continue IRQ handler
    b idle_irq_handler
    
idle_irq_handler:
    ldr pc, [r0, #-12]

# Ensure interrupts are disabled and there is plenty of stack space before calling

flush_sram:
    # Save sound state then disable it
    mov r0, #0x04000000
    ldrh r2, [r0, #0x0080]
    ldrh r3, [r0, #0x0084]
    push {r2, r3}
    strh r0, [r0, #0x0084]

    # Save DMAs state then disable them
    ldrh r3, [r0, #0x00BA]
    push {r3}
    strh r0, [r0, #0x00BA]
    ldrh r3, [r0, #0x00C6]
    push {r3}
    strh r0, [r0, #0x00C6]
    ldrh r3, [r0, #0x00d2]
    push {r3}
    strh r0, [r0, #0x00d2]
    ldrh r3, [r0, #0x00de]
    push {r3}
    strh r0, [r0, #0x00de]

    # Save link register
    push {lr}
    push {r4, r5, r6, r7}
    
    # Try flushing for various flash chips
    adrl r4, flash_save_sector
    sub r4, #0x08000000
    ldr r5, save_size
    adr r6, flash_fn_table 
    adr r7, original_entrypoint 
    
try_flash:
    # Loop through fn table
    ldm r6!, {r2, r3}
    cmp r2, #0
    beq flush_sram_done
    add r2, r7
    add r3, r7
    bl run_from_ram
    cmp r0, #0
    bne found_flash
    add r6, #16
    b try_flash
    
found_flash:
    # Write twice
    ldm r6!, {r2, r3}
    mov r0, r4
    mov r1, r5
    add r2, r7
    add r3, r7
    bl run_from_ram
    ldm r6!, {r2, r3}
    mov r0, r4
    mov r1, r5
    add r2, r7
    add r3, r7
    bl run_from_ram

flush_sram_done:
    # Restore return address and register states
    pop {r4, r5, r6, r7}
    pop {lr}

    # Restore DMAs state
    mov r0, #0x04000000
    pop {r3}
    strh r3, [r0, #0x00de]
    pop {r3}
    strh r3, [r0, #0x00d2]
    pop {r3}
    strh r3, [r0, #0x00c6]
    pop {r3}
    strh r3, [r0, #0x00ba]

    # Restore sound state
    pop {r2, r3}
    strh r2, [r0, #0x0080]
    strh r3, [r0, #0x0084]

    bx lr
    
flash_fn_table:
    .word identify_flash_1
    .word identify_flash_1_end
    .word erase_flash_1
    .word erase_flash_1_end
    .word program_flash_1
    .word program_flash_1_end
    .word identify_flash_4
    .word identify_flash_4_end
    .word erase_flash_4
    .word erase_flash_4_end
    .word program_flash_4 
    .word program_flash_4_end
    .word identify_flash_2
    .word identify_flash_2_end
    .word erase_flash_2
    .word erase_flash_2_end
    .word program_flash_2
    .word program_flash_2_end
    .word identify_flash_3
    .word identify_flash_3_end
    .word erase_flash_3 
    .word erase_flash_3_end
    .word program_flash_3
    .word program_flash_3_end
    .zero 12

run_from_ram:
    # Load code from memory
    push {r4, r5, lr}
    mov r4, sp
    bic r2, #1
    
run_from_ram_loop:
    # And run it    
    ldr r5, [r3, #-4]!
    push {r5}
    cmp r2, r3
    bne run_from_ram_loop

    add r2, sp, #1
    mov lr, pc
    bx r2
    
    # Restore stack pointer and register states    
    mov sp, r4
    pop {r4, r5, lr}

    bx lr

)");

int identify_flash_1() {
    unsigned rom_data, data;
    //stop_dma_interrupts();
    rom_data = *(unsigned *)AGB_ROM;
    
    // Type 1 or 4
    _FLASH_WRITE(0, 0xFF);
    _FLASH_WRITE(0, 0x90);
    data = *(unsigned *)AGB_ROM;
    _FLASH_WRITE(0, 0xFF);
    if (rom_data != data) {
        // Check if the chip is responding to this command
        // which then needs a different write command later
        _FLASH_WRITE(0x59, 0x42);
        data = *(unsigned char *)(AGB_ROM+0xB2);
        _FLASH_WRITE(0x59, 0x96);
        _FLASH_WRITE(0, 0xFF);
        if (data != 0x96) {
            //resume_interrupts();
            for (volatile int i = 0; i < 1024; ++i) {
                __asm("nop");
            }
            return 0;
        }
        //resume_interrupts();
        return 1;
    }
    return 0;
}
asm("identify_flash_1_end:");

void erase_flash_1(unsigned sa, unsigned save_size) {
    // Erase flash sector
    _FLASH_WRITE(sa, 0xFF);
    _FLASH_WRITE(sa, 0x60);
    _FLASH_WRITE(sa, 0xD0);
    _FLASH_WRITE(sa, 0x20);
    _FLASH_WRITE(sa, 0xD0);
    while (1) {
        __asm("nop");
        if (*(((unsigned short *)AGB_ROM)+(sa/2)) == 0x80) {
            break;
        }
    }
    _FLASH_WRITE(sa, 0xFF);
}
asm("erase_flash_1_end:");

void program_flash_1(unsigned sa, unsigned save_size) {    
    // Write data
    SRAM_BANK_SEL = 0;
    for (int i = 0; i < save_size; i += 2) {
        if (i == AGB_SRAM_SIZE) {
            SRAM_BANK_SEL = 1;
        }
        _FLASH_WRITE(sa+i, 0x40);
        _FLASH_WRITE(sa+i, (*(unsigned char *)(AGB_SRAM+i+1)) << 8 | (*(unsigned char *)(AGB_SRAM+i)));
        while (1) {
            __asm("nop");
            if (*(((unsigned short *)AGB_ROM)+(sa/2)) == 0x80) {
                break;
            }
        }
    }
    _FLASH_WRITE(sa, 0xFF);
}
asm("program_flash_1_end:");

int identify_flash_2() {
    unsigned rom_data, data;
    //stop_dma_interrupts();
    rom_data = *(unsigned *)AGB_ROM;
    
    _FLASH_WRITE(0, 0xF0);
    _FLASH_WRITE(0xAAA, 0xA9);
    _FLASH_WRITE(0x555, 0x56);
    _FLASH_WRITE(0xAAA, 0x90);
    data = *(unsigned *)AGB_ROM;
    _FLASH_WRITE(0, 0xF0);
    if (rom_data != data) {
        //resume_interrupts();
        return 1;
    }
    return 0;
}
asm("identify_flash_2_end:");

void erase_flash_2(unsigned sa, unsigned save_size) {
    // Erase flash sector
    _FLASH_WRITE(sa, 0xF0);
    _FLASH_WRITE(0xAAA, 0xA9);
    _FLASH_WRITE(0x555, 0x56);
    _FLASH_WRITE(0xAAA, 0x80);
    _FLASH_WRITE(0xAAA, 0xA9);
    _FLASH_WRITE(0x555, 0x56);
    _FLASH_WRITE(sa, 0x30);
    while (1) {
        __asm("nop");
        if (*(((unsigned short *)AGB_ROM)+(sa/2)) == 0xFFFF) {
            break;
        }
    }
    _FLASH_WRITE(sa, 0xF0);
}
asm("erase_flash_2_end:");

void program_flash_2(unsigned sa, unsigned save_size) {
    // Write data
    SRAM_BANK_SEL = 0;
    for (int i = 0; i < save_size; i += 2) {
        if (i == AGB_SRAM_SIZE) {
            SRAM_BANK_SEL = 1;
        }
        _FLASH_WRITE(0xAAA, 0xA9);
        _FLASH_WRITE(0x555, 0x56);
        _FLASH_WRITE(0xAAA, 0xA0);
        _FLASH_WRITE(sa+i, (*(unsigned char *)(AGB_SRAM+i+1)) << 8 | (*(unsigned char *)(AGB_SRAM+i)));
        while (1) {
            __asm("nop");
            if (*(((unsigned short *)AGB_ROM)+((sa+i)/2)) == ((*(unsigned char *)(AGB_SRAM+i+1)) << 8 | (*(unsigned char *)(AGB_SRAM+i)))) {
                break;
            }
        }
    }
    _FLASH_WRITE(sa, 0xF0);
}
asm("program_flash_2_end:");

int identify_flash_3() {
    unsigned rom_data, data;
    //stop_dma_interrupts();
    rom_data = *(unsigned *)AGB_ROM;
    
    _FLASH_WRITE(0, 0xF0);
    _FLASH_WRITE(0xAAA, 0xAA);
    _FLASH_WRITE(0x555, 0x55);
    _FLASH_WRITE(0xAAA, 0x90);
    data = *(unsigned *)AGB_ROM;
    _FLASH_WRITE(0, 0xF0);
    if (rom_data != data) {
        //resume_interrupts();
        return 1;
    }
    return 0;
}
asm("identify_flash_3_end:");

void erase_flash_3(unsigned sa, unsigned save_size) {
    // Erase flash sector
    _FLASH_WRITE(sa, 0xF0);
    _FLASH_WRITE(0xAAA, 0xAA);
    _FLASH_WRITE(0x555, 0x55);
    _FLASH_WRITE(0xAAA, 0x80);
    _FLASH_WRITE(0xAAA, 0xAA);
    _FLASH_WRITE(0x555, 0x55);
    _FLASH_WRITE(sa, 0x30);
    while (1) {
        __asm("nop");
        if (*(((unsigned short *)AGB_ROM)+(sa/2)) == 0xFFFF) {
            break;
        }
    }
    _FLASH_WRITE(sa, 0xF0);
}
asm("erase_flash_3_end:");

void program_flash_3(unsigned sa, unsigned save_size) {
    // Write data
    SRAM_BANK_SEL = 0;
    for (int i = 0; i < save_size; i += 2) {
        if (i == AGB_SRAM_SIZE) {
            SRAM_BANK_SEL = 1;
        }
        _FLASH_WRITE(0xAAA, 0xAA);
        _FLASH_WRITE(0x555, 0x55);
        _FLASH_WRITE(0xAAA, 0xA0);
        _FLASH_WRITE(sa+i, (*(unsigned char *)(AGB_SRAM+i+1)) << 8 | (*(unsigned char *)(AGB_SRAM+i)));
        while (1) {
            __asm("nop");
            if (*(((unsigned short *)AGB_ROM)+((sa+i)/2)) == ((*(unsigned char *)(AGB_SRAM+i+1)) << 8 | (*(unsigned char *)(AGB_SRAM+i)))) {
                break;
            }
        }
    }
    _FLASH_WRITE(sa, 0xF0);   
}
asm("program_flash_3_end:");

int identify_flash_4() {
    unsigned rom_data, data;
    //stop_dma_interrupts();
    rom_data = *(unsigned *)AGB_ROM;
    
    // Type 1 or 4
    _FLASH_WRITE(0, 0xFF);
    _FLASH_WRITE(0, 0x90);
    data = *(unsigned *)AGB_ROM;
    _FLASH_WRITE(0, 0xFF);
    if (rom_data != data) {
        // Check if the chip is responding to this command
        // which then needs a different write command later
        _FLASH_WRITE(0x59, 0x42);
        data = *(unsigned char *)(AGB_ROM+0xB2);
        _FLASH_WRITE(0x59, 0x96);
        _FLASH_WRITE(0, 0xFF);
        if (data != 0x96) {
            //resume_interrupts();
            for (volatile int i = 0; i < 1024; ++i) {
                __asm("nop");
            }
            return 1;
        }
    }
    return 0;
}
asm("identify_flash_4_end:");

void erase_flash_4(unsigned sa, unsigned save_size) {
    // Erase flash sector
    _FLASH_WRITE(sa, 0xFF);
    _FLASH_WRITE(sa, 0x60);
    _FLASH_WRITE(sa, 0xD0);
    _FLASH_WRITE(sa, 0x20);
    _FLASH_WRITE(sa, 0xD0);
    while (1) {
        __asm("nop");
        if ((*(((unsigned short *)AGB_ROM)+(sa/2)) & 0x80) == 0x80) {
            break;
        }
    }
    _FLASH_WRITE(sa, 0xFF);
    
    for (volatile int i = 0; i < 1024; ++i) {
        __asm("nop");
    }
}
asm("erase_flash_4_end:");

void program_flash_4(unsigned sa, unsigned save_size) {
    // Write data
    int c = 0;
    SRAM_BANK_SEL = 0;
    while (c < save_size) {
        if (c == AGB_SRAM_SIZE) {
            SRAM_BANK_SEL = 1;
        }
        _FLASH_WRITE(sa+c, 0xEA);
        while (1) {
            __asm("nop");
            if ((*(((unsigned short *)AGB_ROM)+((sa+c)/2)) & 0x80) == 0x80) {
                break;
            }
        }
        _FLASH_WRITE(sa+c, 0x1FF);
        for (int i = 0; i < 1024; i += 2) {
            _FLASH_WRITE(sa+c+i, (*(unsigned char *)(AGB_SRAM+c+i+1)) << 8 | (*(unsigned char *)(AGB_SRAM+c+i)));
        }
        _FLASH_WRITE(sa+c, 0xD0);
        while (1) {
            __asm("nop");
            if ((*(((unsigned short *)AGB_ROM)+((sa+c)/2)) & 0x80) == 0x80) {
                break;
            }
        }
        _FLASH_WRITE(sa+c, 0xFF);
        c += 1024;
    }
    
    for (volatile int i = 0; i < 1024; ++i) {
        __asm("nop");
    }
}
asm("program_flash_4_end:");

asm(R"(

# The following footer must come last.
.balign 4
.ascii "2c7deef4686285fe"

# Size of payload
.hword (.+2)
.balign 4

flash_save_sector:
    .end

)");