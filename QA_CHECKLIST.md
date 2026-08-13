# GBA_Emulator — QA Checklist

---

## 1. CPU

- [ ] ARM7TDMI all opcodes correct
- [ ] THUMB all opcodes correct
- [ ] Instruction timing cycle-accurate
- [ ] Interrupt handling correct
- [ ] Conditional execution correct
- [ ] Pipeline behavior accurate

**Pass criteria: 6/6**

---

## 2. Memory

- [ ] DMA timing accurate (immediate, VBlank, HBlank, FIFO)
- [ ] WAITCNT waitstates correct
- [ ] Memory mirroring correct
- [ ] BIOS read protection works

**Pass criteria: 4/4**

---

## 3. Graphics/PPU

- [ ] All 6 modes render correctly
- [ ] Sprites with all sizes/colors
- [ ] Backgrounds with scrolling
- [ ] Windows (WIN0/WIN1) correct
- [ ] VBlank/VCount/HBlank timing

**Pass criteria: 5/5**

---

## 4. Audio

- [ ] Direct Sound channels 1-4 correct
- [ ] FIFO buffer behavior
- [ ] Audio timing matches CPU cycles
- [ ] Volume/envelope correct

**Pass criteria: 4/4**

---

## 5. Integration

- [ ] Opaque C API handle works
- [ ] JNI-ready interface
- [ ] No global state leaks between instances

**Pass criteria: 3/3**

---

## 6. BIOS

- [ ] HLE boot sequence functional
- [ ] No BIOS file dependency

**Pass criteria: 2/2**

---

## 7. Go / No-Go Gate

**Ship when all items pass.**

- [ ] NO ROM/BIOS files in repo
- [ ] All assertions headless (no device required for core tests)
- [ ] core-tests.ps1 and core-benchmarks.ps1 pass
