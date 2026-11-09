# XPC ESP32 — a PC/XT on a TTGO VGA32

**XPC** turns a **TTGO VGA32 v1.4** (ESP32 + PSRAM, VGA, two PS/2 ports, audio jack and SD slot) into an 8088 **PC/XT** that boots MS-DOS, plays the classic CGA/Hercules/EGA games with PC speaker and **AdLib** music, runs Windows 3.0, GEM, GEOS and CP/M-86, and talks to BBSes through a small WiFi modem plugged into the mouse port.

It is built on top of [FabGL](http://www.fabgl.com)'s **PCEmulator** by Fabrizio Di Vittorio, extended and fixed during many evenings of testing real software on real hardware.

> Companion project: **[xpc-minimodem](https://github.com/lgpf/xpc-minimodem)** — a WeMos D1 mini WiFi modem that plugs straight into the TTGO mouse port and becomes COM1, with dial tones and handshake sounds.

---

## Why

The name is a tribute to the **Cobra XPC**, a Brazilian PC/XT compatible from the 1980s. The original goal was simple: boot **SISNE** (the Brazilian Cobra DOS) from a virtual hard disk on a $15 board, with a real keyboard, a real VGA monitor and no PC in between.

From there it grew into a small, self-contained retro PC: pick the machine profile, the video card, the sound card and the hard disk from a text-mode setup screen, keep dozens of disk images on the SD card, and switch between them without a computer.

## Features

**Machine**
- 8088 PC/XT, **256 / 512 / 640 / 704 KB** RAM (704 KB with CGA/Hercules).
- Two boot profiles: **Cobra XPC** (Portuguese messages, memory count, beep) and **IBM PC XT** (classic look).
- Boot from floppy (A:) or hard disk (C:); "no boot disk" message in Portuguese or English.

**Video**
- **CGA** (text, 320x200x4, 640x200x2), **Hercules** (720x348) and **EGA** (320x200x16 / 640x200x16).
- **EGA 640x350** (experimental) — enough for some games.
- Monitor emulation: **color, green, amber and white** phosphor — cycle with `Ctrl+Alt+Down`.
- CGA palette override: `Ctrl+Alt+Right` cycles the 6 CGA palettes, `Ctrl+Alt+Left` restores the program's own.
- Programs that blank the screen on every update (WordStar's CGA "snow" avoidance) no longer make the monitor lose sync.

**Sound**
- **PC speaker**: timer-2 tones plus a "bit-bang" path for games that drive the speaker directly.
  - `Auto` (default): note-based games (Digger, California Games) play as clean tones; digitized sound (Battle Chess) plays as PCM.
  - `PCM` / `Classic` force one mode.
- **AdLib (OPL2)** music.
- **Timer sync** option for games whose music is driven by fast timer interrupts (keeps Digger's music steady; never starves the keyboard).

**Input / ports**
- PS/2 keyboard (US or ABNT2 layout).
- PS/2 mouse (works with MOUSE.COM and CTMOUSE).
- **Mouse port as serial COM1**: `Auto` (mouse if present, otherwise COM1), `PS/2 mouse` or `Serial COM1` — this is where the WiFi mini modem goes.

**Disks and setup**
- Hard-disk images in `\HD`, floppy images in `\DISK` on the SD card.
- Built-in **file manager** for floppies and hard disks: mount, eject, rename, copy, create blank disks/HDs (10–80 MB), delete, write-protect (`XPC.PRT`).
- Hard disks listed by their **description** (from `mconfs.txt`), with size and geometry shown below the list.
- Optional `.chs` file next to an HD image to force its geometry.
- Setup menu with per-item help (`Ctrl+Alt+Up` or `Alt+PrintScreen`), settings saved in flash.

## Tested software

| Category | Titles |
|---|---|
| Operating systems | MS-DOS 3.31 / 4.01 / 5.00 / 6.20, SISNE 3.30, CP/M-86 1.1 |
| GUIs (Hercules) | Windows 3.0, GEM 3.13, GEOS 2.0 |
| Games | Digger, Prince of Persia, Lemmings, Arkanoid II, California Games, Battle Chess, Tetris, SkyChase |
| Tools | WordStar 3/4, Telix 3.22 (with the mini modem), CheckIt |

Known limitations: Digger's death "funeral march" is shortened, Arkanoid II runs a little fast, EGA 640x350 is experimental (GEM's EGA driver does not work — use Hercules), and GMOUSE/GTEST do not detect the mouse.

## Hardware

- **TTGO VGA32 v1.4** (ESP32-WROVER with 4 MB PSRAM). Other FabGL boards with the same pinout should work.
- PS/2 keyboard on the keyboard port; PS/2 mouse **or** the [mini modem](https://github.com/lgpf/xpc-minimodem) on the mouse port.
- VGA monitor, amplified speaker on the audio jack.
- microSD card (FAT32).
- **5 V / 2 A or better power supply** — a PC USB port is not enough (the board resets during the memory count).

## Quick start (no compiling)

1. Install Espressif's **Flash Download Tool** (or `esptool.py`).
2. Flash `bin/merged_0x0.bin` at **0x0** (SPI speed 40 MHz, mode DIO):
   ```
   esptool.py --chip esp32 --port COM9 --baud 921600 write_flash 0x0 bin/merged_0x0.bin
   ```
   To update later, only `bin/firmware.bin` at **0x10000** is needed.
3. Prepare the SD card:
   ```
   \mconfs.txt       hard disk descriptions (see sdcard/mconfs.txt)
   \HD\              hard disk images  (*.img)
   \DISK\            floppy images     (*.img *.ima *.dsk *.vfd *.flp)
   ```
   No disk images are included in this repository — use your own.
4. Power on. Open the setup with `Ctrl+Alt+Up`, choose the hard disk and the machine, and restart.

## Keyboard shortcuts

| Keys | Action |
|---|---|
| `Ctrl+Alt+Up` / `Alt+PrintScreen` | Setup menu |
| `Ctrl+Alt+Down` | Cycle monitor: color → green → amber → white |
| `Ctrl+Alt+Right` / `Ctrl+Alt+Left` | Next CGA palette / program's palette |
| `Ctrl+Alt+Del` | Reset the emulated PC |

## mconfs.txt

One line per disk image; the setup screen and the HD list show the description instead of the file name.

```
desc "SISNE 3.30 (HDD 32MB)" hd0 "HD/hd32_SISNE330.img" chs0 940,4,17 boot hd0
desc "Windows 3.0 Hercules (HDD 20MB)" hd0 "HD/hd20_WINHERC.img" boot hd0
desc "CP/M 86 + Turbo Pascal 3 (floppy 1.44MB)" fd0 "DISK/floppy_CPM86.img" boot fd0
```

`chs0 C,H,S` forces the geometry; without it the geometry comes from a `.chs` file or is detected automatically.

## Building from source

- Arduino IDE / arduino-cli with **ESP32 core 2.0.17**.
- **FabGL 1.0.9** library, with the two patched files from [`fabgl_patch/`](fabgl_patch) copied over the library:
  - `PC8250.cpp` → `FabGL/src/emudevs/` — fixes the emulated 8250 IIR register (Telix froze with traffic in both directions).
  - `serialport.cpp` → `FabGL/src/comdrivers/` — bounded loops when flushing the UART FIFO.
- Board **ESP32 Dev Module**, **Partition scheme: Huge APP**, **PSRAM: Disabled** (PSRAM is enabled at run time by the sketch).
- Open `firmware/PCEmulatorSISNE/PCEmulatorSISNE.ino` and upload.

The BIOS (`bios.asm`, based on 8086tiny) is assembled with NASM into `biosrom.h`.

## Credits

- **FabGL / PCEmulator** — Fabrizio Di Vittorio (GPL v3).
- **8086tiny BIOS** — Adrian Cable (MIT).
- **XPC** — RetroLabs: JL Martins and Luiz Pacheco, 2026.

## License

GPL v3 (see [LICENSE](LICENSE)), as required by FabGL. The BIOS keeps its original MIT license.
