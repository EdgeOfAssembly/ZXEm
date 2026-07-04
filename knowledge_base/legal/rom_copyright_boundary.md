# Spectrum ROM Copyright Boundary — What Is Safe to Re-implement

## The core legal question

The Sinclair ZX Spectrum ROM is a copyrighted computer program. Copyright protects **the expression of the program**, i.e., the exact bytes, the assembly source, the sequence of instructions, labels, comments, and any original structure that reflects creative choice.

Copyright does **not** protect:

- The **functionality** the ROM provides (e.g., "clear the screen", "scan the keyboard", "beep a tone").
- The **hardware interface** it must talk to (ULA ports, keyboard matrix, screen memory layout).
- The **mathematical or logical algorithms** it uses (ASCII-to-number conversion, BCD arithmetic, tape encoding).
- The **API** or entry-point addresses, when they are dictated by hardware convention.

Therefore, we can write a brand-new ROM from scratch that **does the same things** at the **same public addresses**, as long as we do not copy the Sinclair code, structure, or implementation details.

## What is risky / copyrighted

| Risky | Why |
|-------|-----|
| A byte-exact dump of the original 48K ROM | It is the copyrighted work itself |
| Disassembling the ROM and re-typing it with different labels | "Clean-room" requires no exposure to the original; re-typing is still derivative |
| Copying the ROM's internal data tables, error messages, font, or cassette loader byte-for-byte | Original expression |
| Using the ROM's original variable names, comments, or file structure if leaked | Original expression |

## What is safe

| Safe | Why |
|------|-----|
| A synthetic ROM containing only CPU reset/interrupt vectors we wrote | No Sinclair expression |
| Stubs that implement public Spectrum routines from published documentation (e.g., World of Spectrum ROM map) | Facts / functionality, not expression |
| A clean-room reimplementation based on a specification of what each routine must do | New expression of the same functionality |
| Loading a ROM the user supplied from their own hardware | User's own copy; we don't distribute it |

## Our current synthetic ROM

```
0x0000: F3          DI
0x0001: C3 03 93    JP 0x9303
0x0038: FB          EI
0x0039: C9          RET
```

This is unequivocally safe: it is a trivial, independently written program. It does not implement any ROM routine; it only jumps to the snapshot entry point and returns from interrupts.

## Could we expand the synthetic ROM safely?

Yes, but only if we write it from scratch based on **documented behaviour**, not by looking at the Sinclair ROM bytes or a direct disassembly.

### Recommended sources of facts

| Resource | What it documents |
|----------|-------------------|
| [World of Spectrum: ROM disassembly](https://worldofspectrum.org/faq/reference/48kreference.htm) | Public entry points and behaviour (safe to read as a specification) |
| Spectrum hardware reference manuals | Port addresses, screen format, keyboard matrix |
| Sinclair user manual | How BASIC commands work |
| Academic / community write-ups of tape formats | TAP/TZX encoding |

### Process to stay safe

1. Write a **specification** of what each routine must do based on public documentation.
2. Have someone implement it who has **not seen the Sinclair source** (clean-room style).
3. Compare behaviour by running test cases, not by comparing bytes.
4. Do not include original strings, tables, or code sequences.

For a one-person project, the practical rule is: **read only public documentation and write your own code; never look at the Sinclair ROM bytes while implementing.**

## Should you download "official" ROMs temporarily?

**No.** Downloading a copyrighted ROM from the internet is copyright infringement in most jurisdictions, regardless of whether you delete it afterward. The "temporary" nature does not change the legal character of the act. The only clearly lawful ways to obtain a ROM are:

1. Dump it from a Spectrum you own.
2. Obtain it from a source that has a licence from the copyright holder.

Because you value automation, the cleanest path is:

- Make the emulator auto-load a ROM from `rom/` if present.
- If no ROM is present, synthesise a legal minimal ROM.
- Provide clear instructions telling the user how to supply their own ROM if they want full compatibility.

This keeps the project 100% legal while giving the user the option of authenticity.

## Bottom line

- **Dynamic synthetic ROM = legally clear** if it contains no copied Sinclair expression.
- **Expanding the ROM is possible** if you write it from documented behaviour, not from the original bytes.
- **Do not auto-download copyrighted ROMs**, even temporarily.
- **User-supplied ROM from their own hardware = legally clean for us**.
