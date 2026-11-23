# Third-party notices

SKIM's own source code (`src/`, `radio/`, `tools/`) is original work by Pablo Torres, EA5JLJ, under the MIT License (see [LICENSE](LICENSE)).

## Not included in this repository

This repository does **not** include, and does not redistribute:

- any Icom firmware;
- SQ6EMM's custom firmware, its bootloader, or its ICAP runtime files (`ICAPCOLD.BIN`, `ICAPSPOT.BIN`);
- the ICAP SDK itself.

All of them are the separate work of their authors. Get them from their original sources: SQ6EMM's firmware and SDK are at <https://github.com/sq6emm>.

## Used to build `SKIM.APP`

The SKIM radio app is built against the **ICAP SDK by Dawid SQ6EMM** (<https://github.com/sq6emm/icap-sdk>). Its MIT License says: "Copyright (c) 2026 Dawid SQ6EMM". The following SDK parts end up in, or shape, a built `SKIM.APP`:

| SDK file | How it is used |
|---|---|
| `icap.h` | API header. Its inline wrapper functions are compiled into the app. |
| `tools/icap_start.S` | App entry stub, assembled and linked into the app. |
| `tools/app.ld` | Linker script that defines the app's memory layout. |
| `tools/mkapp.py` | Build tool that packs the linked image into the `.APP` file (not part of the app). |

When a built `SKIM.APP` is distributed, the SDK's copyright and license notice applies to those parts:

```
MIT License

Copyright (c) 2026 Dawid SQ6EMM

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## Compiler runtime

`SKIM.APP` is linked with `libgcc` from the GNU Arm toolchain (`arm-none-eabi-gcc`). `libgcc` is covered by the GCC Runtime Library Exception, which allows linking it into programs under any license.

## No other third-party code

The decoder core, the callsign logic, the word-spacing fix and its vocabulary, the desktop tools and the desktop simulator contain no third-party code. The Morse table, the Goertzel and filter code, the Viterbi detector and the WAV reader were all written for this project.
