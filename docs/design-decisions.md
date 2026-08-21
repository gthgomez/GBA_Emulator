# Android product design decisions

**Status:** Approved for implementation (Jun 2026)  
**Drives:** `GbaEmulatorAndroid` — `GbaEmulatorTheme.kt`, strings, launcher brief, P1 ROM/play UI  
**Related:** [android-rom-play-roadmap.md](android-rom-play-roadmap.md) §11

---

## Positioning

| Topic | Decision |
| --- | --- |
| **Metaphor (Q1)** | Software **player** — content-first, minimal chrome during play |
| **Not this (Q2)** | Not a **My Boy–style** clone; not busy retro chrome. **Modern** look that reflects a **strong engine**, not nostalgia cosplay. UI **clean and simple** so attention stays on the **game**. |
| **Promise (Q3)** | **Focus is on the game.** (Use as tagline / empty-state tone; avoid “play all ROMs” or compatibility bragging.) |
| **References embrace (Q25)** | **None** — intentional originality: **different, modern, slick** |
| **References avoid (Q26)** | **Top emulator apps and their UIs** (My Boy, John GBA, RetroArch skins, generic “purple emulator” patterns) |
| **Mood board (Q27)** | Optional — none supplied |
| **Nostalgia dial (Q23)** | **1** — modern instrument |

---

## Visual system

| Topic | Decision |
| --- | --- |
| **Palette (Q4)** | Cool neutrals + one accent |
| **Accent (Q5)** | **Actions only** (primary buttons, FAB) — shell stays quiet |
| **Typography (Q7)** | **Geometric sans** (e.g. Material `FontFamily` with a geometric sans or bundled equivalent) |
| **Theme default (Q20)** | **Light default**; support system override later if needed |
| **High contrast (Q22)** | **High-contrast theme toggle** in settings |
| **Brand placement (Q9)** | **Shell only** — library/bookmark flows branded; **play screen nearly chrome-free** |
| **System bars in play (Q10)** | **Visible, always dark** (OLED-friendly letterbox) |
| **Game surround (Q6)** | **User-selectable** (matte black vs subtle brand gradient) in settings |
| **Pixel scale (Q24)** | **Integer nearest** (crisp pixels) default |

### Token direction (for `GbaEmulatorTheme.kt`)

- **Light `ColorScheme`:** cool gray surfaces (`#F4F6F8` background, `#FFFFFF` cards), ink primary text (`#1A1D21`), muted secondary (`#5C6570`), action accent **cool cyan or steel blue** (not purple) — single hue for `primary` / FAB only.
- **Dark play letterbox:** `#000000` or `#0A0A0C` — not themed shell colors bleeding into viewport.
- **High-contrast variant:** bump contrast ratios (AA+), thicker focus rings, optional pure black/white surfaces.
- **Shapes:** medium corner radius (12dp cards), **no** skeuomorphic buttons.
- **Motion (Q17):** **none** — instant navigation; respect reduce-motion if added later.
- **Density (Q8):** **Spacious** recent/library cards — breathing room, one primary action per row.

---

## Information architecture

| Topic | Decision |
| --- | --- |
| **ROM discovery (Q14)** | **SAF tree bookmark** + **flat recent** list (not full tag library at P1) |
| **List fields (Q15)** | Title (header), last played, save indicator, **user-provided cover only** |
| **Empty state (Q16)** | **Instructional** — short legal-safe copy (“Add a ROM you own…”) aligned with Q3 |

---

## Play experience

| Topic | Decision |
| --- | --- |
| **Touch layout (Q11)** | **Classic** — D-pad left, A/B right (portrait) |
| **Control labels (Q12)** | **Hardware-style** (L/R/SELECT/START) — acceptable because abstract player chrome, not app title “GBA Emulator” |
| **Touch targets (Q21)** | **Larger on play screen** (60dp+) |
| **Haptics (Q13)** | **Off by default**, opt-in |
| **UI sounds (Q18)** | **Branded** subtle UI sounds (not game audio) |
| **UI vs game audio (Q19)** | **Duck UI** when game audio unmuted |

---

## Naming & copy (implementation)

| Surface | Guidance |
| --- | --- |
| **App name** | **Flowframe** |
| **Tagline** | *Focus is on the game.* |
| **Palette** | Arctic variant — shell `#BBE1FA`, accent **sky neon** `#2EC8FF`, play stage `#000000` |
| **Fonts** | **Outfit** (titles/labels) + **Spline Sans** (body) |
| **Play surround default** | **Black** (`PlaySurround.Black`) |
| **Store/listing** | No “#1 GBA emulator” or clone comparisons |

**Theme tokens:** Implemented in `GbaEmulatorAndroid` — `FlowframeColors.kt`, `GbaEmulatorTheme.kt` (Jun 2026).

---

## Anti-patterns checklist

Do **not** ship:

- Purple/violet Material default as primary brand color
- Stock gamepad launcher icon
- Fake plastic device frame as default play UI
- Dense grid of tiny ROM tiles (My Boy library feel)
- Splash screen covered in feature bullets / compatibility claims
- Retro pixel font for all UI text (reserve crisp pixels for **game image** only)

Do **ship**:

- Full-bleed or near full-bleed **240×160** viewport with dark bars
- Quiet shell → one tap from bookmark/recent → play
- Clear load/error states (engine diagnostics surfaced in dev builds)

---

## Implementation order

1. `GbaEmulatorTheme.kt` — light cool scheme, geometric sans, HC toggle stub, surround setting key
2. `strings.xml` + launcher brief (non-gamepad icon)
3. Components: `RomRecentCard`, `GameViewport`, `ControlOverlay` (tokens only)
4. P0 device soak (synthetic) — unchanged
5. P1 SAF bookmark + recent → metadata → game loop per [android-rom-play-roadmap.md](android-rom-play-roadmap.md)

---

## Questionnaire traceability

All §11 answers recorded: Q1–Q24 via forms + Q2–Q3, Q25–Q27 in this document (Jun 2026).
