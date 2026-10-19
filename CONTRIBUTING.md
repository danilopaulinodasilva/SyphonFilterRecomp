# Contributing

Bug reports from playing are the most useful thing right now: which mission, what you did, what you expected, and a screenshot. Widescreen problems at the edges or in the HUD are especially useful.

## Pull requests

- Most fixes belong in the framework, [psxrecomp](https://github.com/danilopaulinodasilva/psxrecomp/tree/syphon-filter) (this project's fork), or in [recomp-ui](https://github.com/danilopaulinodasilva/recomp-ui/tree/feat/psx-menu-title-items): this repo describes the disc, the widescreen sites, the plugin and the menu rows. Nothing game-specific goes into the framework.
- **Never include disc images, BIOS dumps, the recompiled C from `generated/`, or anything ripped from the disc.** Everything derived from the game stays on your machine.

## Where your code comes from

Contributions must be original or MIT-compatible. Don't port code from GPL or non-commercial emulators (Mednafen/Beetle, DuckStation, PCSX and others); use them to understand the hardware and write the code yourself. AI-assisted contributions are welcome if a human understood and verified them.
