# Changelog

Improvements and fixes to SKIM and the decoder, newest first. Each entry says what changed and, where it matters, how it was checked.

## 2026-11-24 — PSKReporter spots (experimental)

### Improvements

- **PSKReporter spots (experimental, `spot=1`).** SKIM can report the CW stations it decodes to PSKReporter, like a small skimmer. It sends the reports itself over the radio's Wi-Fi (`src/pskr.c`: UDP datagrams in PSKReporter's IPFIX format, about every five minutes, always from the same connection), with each station's exact frequency in Hz (the true one with a transverter). The firmware's `@SPOT` service was tried first, but it only adds a positive audio tone to the dial and cannot place a CW station below it, about half the stations in the passband.
  - Only a station that names itself (`CQ X`, `DE X`, `TEST X`, `X TEST`, `X X`) in two different lines with the same letters is spotted, once an hour per band; calls guessed from a QSO next door never are. A first version that spotted on a single line would have sent DL1M, DL1NRL, YP4MCSM and YP40K, all misreads.
  - Needs MY CALL and a position on the radio (spots wait for the locator) and Wi-Fi; nothing is sent while PSKREPORT is off for SKIM (`@MODECFG`). Every station queued and every datagram sent is logged.
  - Checked: the receiver part of the datagram matches the example on pskreporter.info byte for byte; an independent decoder reads every field back (5-byte frequencies, negative S/N, sequence numbers, records split over several datagrams); in the simulator, which now answers `@MYCALL`, `@MYLOC`, `@MODECFG` and `@FREQ` and writes the datagrams to `PSKR.DAT` instead of sending them, the ten recordings give six correct calls and nothing when PSKREPORT is off, MY CALL is missing or there is no position yet. ASan/UBSan clean.
  - The station table moved from the app window to the heap, to keep room in the 80 KiB window (code, variables and stack now 75 KB). The recordings give exactly the same text.
  - Off by default.

## 2026-11-24 — documentation: upgrading

- **Upgrading without losing your settings.** The installation steps said to copy `SKIM.CFG`, which on an upgrade would overwrite your `pitch`, `cwr` and other settings. Both READMEs now say: when upgrading, copy only `SKIM.APP` and `SKIMDICT.BIN` and keep your `SKIM.CFG`; settings added later take their defaults.

## 2026-11-24 — documentation: 8-app limit

- **The radio lists at most 8 apps.** With SQ6EMM's full app bundle in `Apps`, SKIM does not appear in the Apps menu or on the RTTY button until one unused app is moved out of the folder. Found by a user and confirmed: it had also happened on the first install here. Now in the installation steps of both READMEs and in the known limitations.

## 2026-11-24 — ragchew values, cut numbers, self-QSO labels

### Improvements

From a recorded ragchew between IV3ZXF and 4O3RB/OE9IBI, full of glued words:
- **Values after RIG / PWR / WX IS:** `RIGISYAESUFT` → `RIG IS YAESUFT`, `PWRIS2TTW` → `PWR IS 2TTW`, `WXISSUNNY` → `WX IS SUNNY`. Not after ANT or OP, which would split ANTISEPTIC and OPISTHOGRAPH.
- **Numbers in cut figures** (T or O for 0, N for 9, W for watts) count as numbers: `2TTW`, `100W`, `58N`. A call whose suffix is cut figures ending in W (`IS2TTW`, `IS100W`) is read as IS + power. Calls such as OK1TN are not affected, nor contest exchanges such as `5NNT36`.
- **Places after NR (near):** `NRTRIESTE` → `NR TRIESTE`, `QTHNRTRIESTE` → `QTH NR TRIESTE`. A name or place at the end now costs half its length, and a two-letter word that is no CW word closing a token costs more, so a name is not cut into words (`TRIES TE`).
- **`DROM` → `DR OM`, `73 73` before `7373`** (`7373CIAOFROMITALY` → `73 73 CIAO FROM ITALY`); CIAO and country names added to the English list; WX may anchor a split (`WXSUNNYSUNNY` → `WX SUNNY SUNNY`).

### Fixes

- **A station shown working itself.** `OE9EBI>OE9IBI` and `ON8V>ON8VA`: a QSO partner one character away from the station's own call is a misread of it and is no longer shown.

Checked: the ten recordings and all radio logs change only for the better (no letter changed); 234,000-word dictionary still 37 split, 50,000 moderately common words still 10, 1,307 common words still 0; half a million random lines under ASan/UBSan; stack still about 11 of 16 KB.

## 2026-11-24 — code review fixes

### Fixes (code review)

Found by a full review: clang static analyser, AddressSanitizer and UndefinedBehaviorSanitizer on every tool, recording and test file, half a million random lines through the word-spacing fix and callsign tracker, extreme audio (silence, clipping, DC, full-scale noise, 37 tones at once, a 3-sample file), stack use measured on the radio build, and all benchmarks compared with `results/` (identical).

- **Stack use on the radio.** The 3 KB settings buffer was inlined into `app_main` and stayed on the stack for the whole session: the deepest call chain (app → line → word spacing) took about 14 of the 16 KB of stack. `read_cfg` is no longer inlined; the deepest chain is now about 11 KB.
- **Letters lost on very long lines.** The word-spacing fix kept only the first 64 pieces of a line and dropped the rest (79 `=` signs in one line would do it). Text after the 64th piece is now copied as it is; random testing checks that only spaces ever change.
- **A damaged `SKIMDICT.BIN` could overflow a buffer.** The word list was trusted: a corrupted file could make a lookup write past a 16-byte buffer. Lookups now stop at any inconsistent entry, and SKIM rejects a file whose offsets are out of order (the log says `damaged or too big`, not `not found`).
- **Trailing `?` with no room.** Putting back the `?` after a split token (`HW?`) did not check the space left; a long token followed by many `?` could overflow. Such a token is now left whole.
- **Malformed WAV files** (desktop tools): a bit depth below 8 divided by zero and a short `fmt` chunk read past the end of the file; both are now rejected.

## 2026-11-24 — more CW abbreviations

### Improvements

- **More CW abbreviations.** About 55 common abbreviations from published CW lists (W5WWW, W3DF, giangrandi.org, Wikipedia) are now known to the word-spacing fix, so glued ones split: `TNXFERRCVDSIGS` → `TNX FER RCVD SIGS`, `SRYQRMHVY` → `SRY QRM HVY`, `MYRIGXCVR100WTS` → `MY RIG XCVR 100 WTS`, `TDYCLRWX` → `TDY CLR WX`, `CONGRATSOM` → `CONGRATS OM`. Among them: ARND, BTR, BTW, CUD, WUD, SHUD, CUZ, ENUF, KNW, LSN, SND, TMW, TDY, THX, YRS, HNY, UFB, CLDY, CLR, OVC, SNW, HVY, RCVR, XCVR, TCVR, XMTR, XTAL, STN, WTS, SIGS, FREQ, CNTCT, MSG, BURO, WKD, WRK, WPM, RCVD, PFX, QSLL, GRT, SRY.
  - Left out after measuring: the two-letter ones (TX, RX, HV, WL, BN…), because they stopped split callsigns from being joined (`DF8 WL` stayed apart); CUAGN, which kept `CU AGN` glued; VERT, LIL and SKED, which split CULVERT, GALILEE and TUSKED. ANS, URS, WID, WUD, GND and OPR are weak words, as they hide inside English words (DEANS, DETOURS).
  - Only a fixed list of unmistakable abbreviations (QTH, QSO, TNX, HPE, RPRT…) may anchor a split into English words, so the new ones never split words like VERTICES or SUBTRACT.
  - Checked: no change on any radio log, 234,000-word dictionary still 37 split, 50,000 moderately common words still 10, no letter changed.

## 2026-11-24 — Spanish words, own English list (all MIT)

### Improvements

- **Own English word list: the whole project is MIT.** The English words no longer come from SCOWL: `tools/words_en.txt`, written for this project like the Spanish list, holds common English words and CW/radio vocabulary (weather, rigs, antennas, family, travel), and `tools/mkdict.py` generates plurals, verb forms and comparatives (about 4,500 forms). SCOWL's notice and `third_party/` are gone. Measured against the SCOWL-based list: all radio logs and the seven recordings give the same text, except one more fix (`ANT ENNA` → `ANTENNA`, a radio word SCOWL lacked); the four ragchew fixes still work; none of the 1,307 common SCOWL words missing from the new list is split; 234,000-word English dictionary: 37 split (38 before). SCOWL was used only on the computer as a yardstick, never to choose the words.
- **Spanish words in the word list.** `SKIMDICT.BIN` now also holds about 6,000 common Spanish forms, written for this project in `tools/words_es.txt` (plain words, nouns with plurals, adjectives with feminines, regular and irregular verbs with their usual forms, radio vocabulary). Accents are removed and Ñ becomes N, as in CW (`AÑO` → `ANO`). Split words are joined (`GRA CIAS` → `GRACIAS`, `ANT ENA` → `ANTENA`, `CONTA CTO` → `CONTACTO`) and glued phrases split around a CW word (`GRACIASPORELQSO` → `GRACIAS POR EL QSO`, `RST599QTHMADRID` → `RST 599 QTH MADRID`). A name or place may follow QTH or NOMBRE, with ES or IS in between: `QTHESVALENCIA` → `QTH ES VALENCIA`, `MINOMBREESPABLO` → `MI NOMBRE ES PABLO`. The list grows from 15 to 32 KB, still well inside the free memory. `src/dict_en.c/.h` are now `src/dict.c/.h`.

  Checked: all radio logs and all seven recordings give exactly the same text as with the English-only list; 50,000 moderately common English words: no new splits; 234,000-word English dictionary: 38 split (35 with the English-only list), all rare. No Spanish recording yet.

### Fixes

- **A number is not enough to split a phrase.** `73ALAN` (a name after 73) would have become `73 AL AN` once `AL` and `AN` were words: a split into words now needs a real word of 3+ letters besides the anchor.
- **The 4+ letter word before DR or NAME must come before it.** `NAME` itself counted, so `UNORNAMENTAL` could split as `UN OR NAME NTAL`.

## 2026-11-24 — QSO sides, English word list, smaller app

### Fixes

- **Both sides of a QSO shared one label.** Signals up to 80 Hz apart counted as one station, so in a QSO between G3PQD (601 Hz) and IN3TWX (574 Hz) everything IN3TWX sent was labelled `G3PQD>IN3TWX`. Stations are now told apart above 20 Hz (one station's tone varies by ±3 Hz). The same change removed wrong labels on nearby callers (a station at 947 Hz labelled as the one at 903 Hz).

### Improvements

- **English word list for word spacing.** In conversations many operators space words no wider than letters, or letters as wide as words: in a recorded ragchew, IN3TWX's word gaps were 3.3–3.7 units, like his letter gaps, and one of G3PQD's letter gaps inside `THERE` (4.7 units) was longer than his gap between `THE` and `LAST` (4.6). Timing alone cannot settle that. SKIM now reads an optional list of about 4,000 common English words (SCOWL size 10) from `SKIMDICT.BIN`:
  - two pieces that make a common word are joined when they are not both words: `TH ERE` → `THERE`, `PER HAPS` → `PERHAPS`, `IN FO` → `INFO`, `TE ST` → `TEST`;
  - a glued phrase is split into English and CW words around an unmistakable CW abbreviation (HPE, TNX, QTH, RPRT, ABT…) or a number, or before a name after DR: `HPECUINPERSON` → `HPE CU IN PERSON`, `THEMEETINGSDRDEREK` → `THE MEETINGS DR DEREK`;
  - words on the list are never split.
  Checked on seven recordings and all radio logs (only the intended lines change, no letter changed), on 50,000 moderately common English words not on the list (none split by the new rules) and on a 234,000-word dictionary (34 split, against 32 before, all rare). Without `SKIMDICT.BIN`, SKIM works as before; the log header says whether it was loaded. Tested on the radio: the list loads (`# word list: SKIMDICT.BIN, 15228 bytes`) and the app runs with the new memory layout. Built by `tools/mkdict.py`; SCOWL's notice is in `THIRD_PARTY_NOTICES.md`.
- **`PSEK` → `PSE K`.** A final K after strong CW words is split off (`PSEK`, `AGNK`, `HPEK`, `TNXK`); it was blocked by the rules that keep plain words whole. `GEEK`, `BACK`, `TANK` stay whole.
- **Smaller app.** The text handling (word spacing, callsigns, the app itself) is now built for size and the settings buffer lives on the stack: 44 KB of code and 9.5 KB of variables instead of about 51 KB and 13.6 KB, so the app, its variables and its stack fit the 80 KiB app window of the IC-705 (and the 76 KiB of the IC-9700) with room to spare. The decoder itself is still built for speed.
- **QSO partner labels.** Often only one side sends both calls (`IN3TWX DE G3PQD`). A station with no callsign of its own, within 100 Hz of a station in a QSO heard in the last 2 minutes, is now labelled as the other side: `IN3TWX>G3PQD`. Checked on recordings of real QSOs: both sides of three QSOs are now labelled correctly (G3PQD/IN3TWX, ON6FT/ON6IIT, YP40CSM/IK1YRA).

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
