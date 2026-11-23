# Changelog

Improvements and fixes to SKIM and the decoder, newest first. Each entry says what changed and, where it matters, how it was checked.

## 2026-11-23 — word spacing in ragchew QSOs

### Improvements

- **Word spacing in ragchew QSOs.** In a real QSO many glued words stayed glued. Three causes, all fixed:
  - a token with `=` (BT) or `+` was never split. `=` and `+` now always stand apart: `TNXFERRPRT=URRSTALSO5NN5NNQSB=` → `TNX FER RPRT = UR RST ALSO 5NN 5NN QSB =`, `TRIESTE=HW?` → `TRIESTE = HW?`;
  - without a callsign or report in the token, no weak word (FER, CALL, DR…) was allowed, so `TNXFERCALL` stayed glued. Weak words are now allowed next to strong ones in tokens of 6+ letters: `TNX FER CALL`, `FB TNX CALL`;
  - missing words: `CU`, `SN`, `HPE`, `RPRT`, `ALSO`, `MNI`, `GUD`, `CONDX` (`CUAGNSN` → `CU AGN SN`). `AR` was removed (`GEAR` was split).
- **Names and places.** Right after `DR`, `NAME` or `QTH` (or `NAME IS`, `QTH IS`, `OP IS`, or `DR OM` after a greeting such as GE or TNX), one unknown word may follow: `GEDRALFON` → `GE DR ALFON`, `GEDROMALFONSO` → `GE DR OM ALFONSO`, `QTHISCADIZ` → `QTH IS CADIZ`. `OM` alone does not count, or `GEOMETRY` would split.
- **`VY7373` → `VY 73 73`** (four-digit numbers cost more than two 73s).

Checked on all the radio logs (28 lines improve, no letter changed) and on a 234,000-word English dictionary: 32 plain words are split, against 18 before (all rare: GUDGEON, GADROON, TESTES…); TOUR, DETOUR, HIDE, GLAND, FORGE stay whole.

## 2026-11-23 — contest, CPU and dial update

### Improvements

- **Contest CQ.** `TEST X` or `X TEST` now shows the station as calling CQ (`CQ E77EA:`). Before, a contest station kept the label of its last QSO (`E77EA>Z35M: TEST E77EA`).
- **Pause while the dial turns.** When the dial moves more than 50 Hz in 0.1 s, SKIM closes the open lines and stops decoding. Once the dial has been still for 0.4 s, it starts afresh on the new frequency. Small tuning steps do not pause it. On the radio, the load with the dial still dropped to 4–5 % (from 18–20 %). The `# load:` log line now counts `dial pauses`.
- **Less CPU in the tone search.** The noise floor no longer sorts the spectrum twice per frame (one of the sorts was never used). Two order statistics are now found by selection instead. In desktop tests the decoder is 20–28 % faster, with exactly the same output on the benchmarks and test files.
- **More responsive radio.** SKIM gives the CPU back to the radio after every 20 ms block of audio, instead of every 8 blocks.
- **Clipped first element.** Many transmitters in semi break-in cut the first element short while they switch to TX, so `TEST` arrived as `EEST` or `EST`. After a pause of 0.8 s or more, the first element now counts as a dash from 1.5 units instead of 2. In synthetic tests with the first element cut by 1.4 units, 81 of 144 `TEST`s were read correctly (9 before), and none of 360 dot-first words was misread. Benchmarks unchanged.
- **Load line in the log.** `# load:` also shows the longest block time (`longest block: N ms`).
- **`cwgen -c ms`.** The synthetic generator can clip the first element of each transmission, to test the fix above.

### Fixes

- **A bare `CQ` before unlabelled text.** `CQ DH HS TEST` read as if `CQ` were decoded text. `CQ` now appears only together with a callsign.
- **Contest serial numbers taken as callsigns.** `T16C` (serial 016 in cut numbers) was accepted as a call. Prefixes T0 and T1 are not allocated and are now rejected.

## 2026-11-23 — first public release

First version on GitHub (`ea5jlj/skim-ic705`), built for SQ6EMM firmware 20261119v14.

- Multi-signal CW decoding on the IC‑705 DECODE screen: up to 12 stations at once, each with its real frequency, callsign, CQ and QSO partner (`CALL>OTHER`).
- Operator profiles per channel, so two stations alternating on one frequency keep their own timing and labels.
- Word-spacing fix with a CW vocabulary and the callsign rules.
- Crosstalk gate and a pile-up–proof noise floor.
- Desktop tools: synthetic generator, WAV decoder, benchmark, ICAP simulator.

Also in this release, after the first radio tests:

- **Log rotation.** Only the last 10 session logs are kept (setting `logs=`, 1–98). Older ones are deleted at start, and the numbers go round from 99 to 01.
- **Special-event callsigns.** Suffixes of up to 6 letters are accepted when the call has two or more digits (`HA40EUDXF`). A callsign cut short (`HA40EU`) supports the full one instead of competing with it.
- **`SKIM.CFG` read only in part.** Only the first 1024 bytes were read, so every setting after `pitch` (`cwr`, `dcols`, `decode`, `log`, the window settings) was silently ignored. The whole file is now read.
- **Glued words with a report inside.** `GM5NN73TUEE` was split as `GM5N N73TUEE`. The word-spacing fix no longer cuts inside a report, and calls with several digits cost more: `GM 5NN 73 TU EE`, `DL8MKG 73 TU`.
- **Noise stuck to a word.** `IIICQDX` stayed glued. Runs of `I` are now split off and removed by the noise filter: `CQ DX`.
