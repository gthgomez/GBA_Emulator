#!/usr/bin/env python3
"""Generate small, legally distributable synthetic GBA ROMs for host tests.

Two fixtures:

  smoke    Entry code loads a value, then branches to itself. Produces a
           valid cartridge header but no graphics; used for deterministic
           headless determinism runs.
  video    Configures DISPCNT for mode 3 (BG2), then writes alternating
           direct-color pixels into VRAM and spins. Produces genuinely
           nonuniform graphics so an interactive host can be checked for a
           rendered (rather than black) window.

Nothing here is derived from any copyrighted ROM; the code is emitted by the
tiny ARM assembler below.
"""

import argparse
import struct
import sys

# --- Minimal ARM assembler (only the encodings these fixtures need) --------


def rol32(value, shift):
    shift &= 31
    if shift == 0:
        return value & 0xFFFFFFFF
    return ((value << shift) | (value >> (32 - shift))) & 0xFFFFFFFF


def ror32(value, shift):
    return rol32(value, (32 - shift) & 31)


def encode_immediate(value):
    """Return (rotate, imm8) with ROR(imm8, 2*rotate) == value."""
    for rotate in range(16):
        shift = (2 * rotate) & 31
        imm8 = rol32(value, shift)
        if imm8 <= 0xFF and ror32(imm8, shift) == value:
            return rotate, imm8
    raise ValueError(f"value 0x{value:08X} is not ARM-immediate encodable")


def mov_imm(rd, value):
    rotate, imm8 = encode_immediate(value)
    return 0xE3A00000 | (rd << 12) | (rotate << 8) | imm8


def add_imm(rd, rn, value):
    rotate, imm8 = encode_immediate(value)
    return 0xE2800000 | (rn << 16) | (rd << 12) | (rotate << 8) | imm8


def cmp_imm(rn, value):
    rotate, imm8 = encode_immediate(value)
    return 0xE3500000 | (rn << 16) | (rotate << 8) | imm8


def bne(target, current):
    imm24 = ((target - (current + 8)) >> 2) & 0xFFFFFF
    return 0x1A000000 | imm24


def strh_imm(rd, rn, offset):
    assert 0 <= offset <= 0xFF, "STRH immediate offset out of range"
    return 0xE1C00000 | (rn << 16) | (rd << 12) | (0xB << 4) | offset


def strh_post(rd, rn, offset):
    assert 0 <= offset <= 0xFF, "STRH post-index offset out of range"
    return 0xE0C00000 | (rn << 16) | (rd << 12) | (0xB << 4) | offset


def branch_here():
    # A bare branch-to-self (B #-8) currently runs away in the engine, so the
    # idle loop uses the proven two-instruction idiom: a no-op followed by a
    # branch back to it (B #-12).
    return [0xE1A00000, 0xEAFFFFFD]


def build_smoke_code():
    return [0xE3A00301] + branch_here()


def build_video_code():
    # Code must stay below 0xA0 (the cartridge header title lives there), so
    # the framebuffer fill is a compact counted loop rather than unrolled.
    base = 0x08000000
    code = [
        0xE3A00301,               # mov      r0, #0x04000000  (IO base)
        0xE3A01B01,               # mov      r1, #0x0400      (BG2 enable)
        add_imm(1, 1, 0x3),       # add      r1, r1, #3       (mode 3)
        strh_imm(1, 0, 0),        # strh     r1, [r0]        (DISPCNT)
        0xE3A02406,               # mov      r2, #0x06000000  (VRAM base)
        mov_imm(3, 0x001F),       # mov      r3, #0x001F      (blue)
        mov_imm(4, 0x7C00),       # mov      r4, #0x7C00      (red)
        0xE3A05000,               # mov      r5, #0           (byte counter)
    ]
    loop_index = len(code)
    loop_addr = base + loop_index * 4
    code.append(strh_post(3, 2, 2))       # strh r3, [r2], #2
    code.append(strh_post(4, 2, 2))       # strh r4, [r2], #2
    code.append(add_imm(5, 5, 4))         # add  r5, r5, #4
    code.append(cmp_imm(5, 240 * 160 * 2))  # cmp r5, #0x12C00
    branch_index = len(code)
    code.append(bne(loop_addr, base + branch_index * 4))  # bne loop
    code.extend(branch_here())  # idle spin (two-instruction loop)
    return code


def build_bad_code():
    # 0xFFFFFFFF is an undefined encoding (NV condition): the engine must
    # abort the run with a non-zero exit code rather than silently continue.
    return [0xFFFFFFFF]


def build_sram_code():
    # Declares SRAM-backed saving (the "SRAM_V" marker injected into the image)
    # and increments the first save byte once per boot. Successive runs of the
    # host therefore produce an observable 0x00, 0x01, 0x02, ... progression
    # only if the cartridge save is written on exit *and* reloaded at startup.
    return [
        mov_imm(2, 0x0E000000),   # r2 = SRAM window base
        0xE5D23000,               # ldrb r3, [r2]
        add_imm(3, 3, 1),         # add  r3, r3, #1
        0xE5C23000,               # strb r3, [r2]
    ] + branch_here()


def wrap_rom(code_words, title=b"SYNTHTEST", marker=None, marker_offset=0x40):
    rom = bytearray()
    for word in code_words:
        rom += struct.pack("<I", word)
    # Pad so the header fields sit inside the image and the size is word
    # aligned (mirrors real cartridges closely enough for the loader).
    if len(rom) < 0x200:
        rom += bytes(0x200 - len(rom))
    title = title[:12].ljust(12, b"\x00")
    rom[0xA0:0xAC] = title
    rom[0xB2] = 0x96                     # fixed value
    if marker is not None:
        rom[marker_offset : marker_offset + len(marker)] = marker
    checksum = 0
    for byte in rom[0xA0:0xBD]:
        checksum = (checksum - byte) & 0xFF
    rom[0xBD] = (checksum - 0x19) & 0xFF
    return bytes(rom)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kind", choices=["smoke", "video", "bad", "sram"], required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    if args.kind == "smoke":
        rom = wrap_rom(build_smoke_code(), b"SMOKETEST")
    elif args.kind == "bad":
        rom = wrap_rom(build_bad_code(), b"BADSYNTH")
    elif args.kind == "sram":
        rom = wrap_rom(build_sram_code(), b"SRAMTEST", marker=b"SRAM_V")
    else:
        rom = wrap_rom(build_video_code(), b"SYNTHVID")

    with open(args.output, "wb") as handle:
        handle.write(rom)
    print(f"make-synthetic-rom: wrote {args.kind} fixture ({len(rom)} bytes) to {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
