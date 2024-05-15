# No battery patcher for GBA roms

This program patches a GBA rom for batteryless saving. (i.e. saving on a bootleg cartridge containing SRAM but no battery)

The game must be SRAM patched before using this program. GBATA or [Flash1M_Repro_SRAM_patcher](https://github.com/bbsan2k/Flash1M_Repro_SRAM_Patcher) can be used depending on the game.

The patch contains two modes, which can be selected during patching:
- In auto mode (0), the save will automatically be flushed a few seconds after the in-game save.
- In keypad/manual trigger mode (1), the save can be flushed by pressing L+R+Start+Select at any time. This mode requires less patching, so may be compatible with more games.

The game will freeze when flushing has started and will unfreeze when flushing has completed.

## Usage
Run with the rom file as the only argument and a new rom will be generated. (For GUI users, drag the rom onto the .exe in the file browser)

## Building
Under Linux run:

`./make.sh`

(Requires: gcc, xxd, devkitpro and mingw-w64)

## Credits
Written by [metroid-maniac](https://github.com/metroid-maniac/) and [r3mko](https://github.com/r3mko/)

Thanks to:
- [ez-flash](https://github.com/ez-flash) for [EZ Flash Omega kernel](https://github.com/ez-flash/omega-kernel) containing examples for hooking the IRQ handler
- [Fexean](https://gitlab.com/Fexean) for [GBABF](https://gitlab.com/Fexean/gbabf)
- [vrodin](https://github.com/vrodin) for [Burn2Slot](https://github.com/vrodin/Burn2Slot)
- [Lesserkuma](https://github.com/lesserkuma) for [FlashGBX](https://github.com/lesserkuma/FlashGBX) and batteryless versions of [Goomba Color](https://github.com/lesserkuma/goombacolor) and [PocketNES](https://github.com/lesserkuma/PocketNES)
- [Ausar](https://github.com/ArcheyChen) for helping to port the payload to C.
