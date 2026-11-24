# SKIM — multi-signal CW decoder for the Icom IC-705

**Author:** Pablo Torres, EA5JLJ

SKIM is an app for the IC-705 running **SQ6EMM's custom firmware** (ICAP platform). It decodes the CW signals inside your receiver passband at once and shows them on the radio's own **DECODE screen**, one line per station. The radio stays in real **CW mode**, so the dial, filters, BK-IN and your key keep working.

```
7025.0 CQ EA4XYZ: CQ EA4XYZ EA4XYZ
7025.0 EA4XYZ>DL1ABC: TU EA4XYZ
7026.3 F5XYZ: 73 DX DE F5XYZ
```

Each line shows the station's **real frequency**, **CQ** when it is calling CQ, its **callsign**, and **who it is working** (`CALL>OTHER`) once that is known. More examples from real logs (a CQ and its callers, both sides of a QSO, several stations at once) are in [What the output looks like](../README.md#what-the-output-looks-like).

SKIM is receive-only: it never transmits.

> Independent project, not affiliated with, authorized by, or endorsed by Icom Inc. or SQ6EMM. Using SKIM requires modified radio firmware, at your own risk. See [Notices](#notices).

## Requirements

- Icom IC-705 (E or J) with SQ6EMM custom firmware, release **20261119v14**. The patcher and instructions are at <https://sq6emm.github.io/icom-custom-firmware/>.
- The ICAP runtime files from the same release (`ICAPCOLD.BIN`, `ICAPSPOT.BIN`) in `\IC-705\Apps\`. They come in the firmware's apps bundle.

## Install

The compiled app is in the repository's [`app/`](../app/) folder: `SKIM.APP`, `SKIM.CFG` and the English and Spanish word list `SKIMDICT.BIN`, ready to use. It is built for SQ6EMM firmware release **20261119v14**; with another release, build it yourself against the matching SDK (see Building below).

| File | SHA-256 |
|---|---|
| `app/SKIM.APP` | `2b8e2978de609b95cd4751ba9c2c203428e6679b7e3163a23601fb76dbe630bf` |
| `app/SKIMDICT.BIN` | `652ca556d0ee18fd15fcc879471a3e0b69d9360939ed546ef2379bc01e7fbda3` |

1. Copy `SKIM.APP`, `SKIM.CFG` and `SKIMDICT.BIN` from `app/` to `\IC-705\Apps\` on the SD card, next to `ICAPCOLD.BIN` and `ICAPSPOT.BIN`. Keep the names in capitals as they are. `SKIMDICT.BIN` is optional: without it SKIM works the same, but cannot use English and Spanish words to fix word spacing in conversations.

   **At most 8 apps.** The radio lists at most 8 apps. If SQ6EMM's full app bundle (FT8, JT4, PI4, Q65, WSPR…) is already in `Apps`, SKIM does not show up, neither in **MENU › SET › Apps** nor on the RTTY button, and a rescan may stay on "reading SD card". Move one app you do not use (its `.APP` and `.CFG`) to a folder outside `Apps`, and SKIM appears. Confirmed by two users; not documented in the SDK.

   **Upgrading.** Copy only `SKIM.APP` and `SKIMDICT.BIN` over the old ones, and **keep your `SKIM.CFG`**: it holds your `pitch`, `cwr` and other settings, and copying the new one would reset them. Settings added in newer versions take their default values when they are missing from your file; to use one, copy its line from the new `SKIM.CFG` into yours. [CHANGELOG.md](../CHANGELOG.md) lists what changed in each version.
2. Set your **CW PITCH** in `SKIM.CFG` (`pitch=600`). The app cannot read it from the radio, and it is needed to turn each station's audio tone into its real frequency. 600 Hz is the IC-705 factory default. To see yours, press the **MULTI** knob in CW mode and look at **CW PITCH**, then put the same value here. If it does not match, every frequency shown will be off by the difference (for example 200 Hz off if your pitch is 800 Hz). If you use CW-R, also set `cwr=1`.

## Use

1. Put the radio in **CW**.
2. Press **RTTY** repeatedly (RTTY → RTTY-R → **SKIM**). SKIM starts and the **SKIM DECODE** screen appears.
3. Press the mode button and go back to **CW**. The SKIM DECODE screen stays, SKIM keeps decoding, and the radio is in real CW mode.
4. Use a **wide filter** (FIL1, 1.2 kHz or more) to see several stations, or a narrow one to focus on one.
5. To check the pitch setting, tune one station until you hear it at your usual CW tone: SKIM should show it at the dial frequency (±0.1 kHz). If it is always off by the same amount, `pitch` in `SKIM.CFG` does not match the radio.

After switching the radio off and on you have to select SKIM again. Starting it automatically (`AUTORUN.CFG`) does not help: the firmware shows "RTTY DECODE" and does not pass anything to the app.

If the radio stays out of CW (for example in RTTY-R), SKIM keeps decoding but shows the **audio tone** instead of a frequency. After 8 s it shows a one-time reminder to switch back to CW.

The small graph on the DECODE screen is the radio's own spectrum of the **audio** after the filter. That is why it narrows and widens with FIL1/2/3.

## Settings (`SKIM.CFG`)

| Key | Default | Meaning |
|---|---|---|
| `pitch` | 600 | Your radio's **CW PITCH** (Hz): MULTI knob in CW mode. 600 = IC-705 factory default. Needed for correct frequencies (see Install, step 2). |
| `cwr` | 0 | Set to 1 for CW-R, or if "CW Normal Side" is set to USB. The radio does not tell apps which one is active. |
| `mode` | multi | `multi`: several stations at once. `single`: only the strongest station. |
| `chans` | 8 | Stations decoded at once in multi mode: 1–12 (1–4 with `det=vit` or `auto`). More helps in a pile-up; fewer gives a calmer screen. |
| `tmin` / `tmax` | 250 / 2500 | Audio tone search range (Hz). It should cover your filter. |
| `calls` | 1 | Show callsign, CQ and QSO partner. |
| `clean` | 1 | Remove typical noise from lines (stray E/T, `*`). The log also keeps the raw text. |
| `words` | 1 | Fix word spacing with a small CW vocabulary and the callsign rules (see How it works). Only spaces change, never letters. |
| `pause` | 1500 | ms of silence that close a line (lower = faster, shorter lines). |
| `dcols` | 40 | Line width on DECODE and in the log. |
| `det` | thr | Detector: `thr` (threshold), `vit` (Viterbi) or `auto`. `thr` works best on real audio so far. |
| `log` | 1 | Write `SKIMnn.LOG` (one file per session) in the Apps folder. |
| `logs` | 10 | How many session logs to keep (1–98). At start SKIM deletes the older ones. |
| `spot` | 0 | 1 = report CW stations to PSKReporter over Wi-Fi (experimental, see [PSKReporter spots](#pskreporter-spots-experimental)). |
| `cols`, `spec`, `fonttest` | | Only for the app's own window (MENU > SET > Apps > SKIM), which is not needed in normal use. |

## How it works

- **Tones:** a Goertzel bank with a Hann window finds the CW tones in the audio. The noise floor is measured only inside the radio's filter passband.
- **Per station:** each station gets its own channel: I/Q mix, a low-pass filter adapted to its speed, an envelope with adaptive threshold, speed tracking (k-means on mark/space lengths) and Morse lookup.
- **Frequency:** station = dial − (tone − pitch). In normal CW, Icom receives on the LSB side, so a higher tone is a lower station.
- **Clipped first element:** many transmitters in semi break-in cut the first element short while they switch to TX (`TEST` arrives as `EEST`). After a pause of 0.8 s or more, the first element is read as a dash from 1.5 units instead of 2.
- **Turning the dial:** while the dial moves fast (more than 50 Hz in 0.1 s), every tone moves and nothing can be decoded, so SKIM closes the open lines and pauses. Once the dial has been still for 0.4 s it starts afresh on the new frequency. Small steps do not pause it.
- **Only in CW:** outside CW (the decoder mode of the RTTY button, SSB, FM…) SKIM pauses: it closes the open lines and stops decoding until the radio is back in CW, where it starts afresh. After 8 s out of CW the radio shows `SKIM paused. Switch to CW mode.` (once per pause). The log writes `# paused: radio not in CW` and `# resumed: CW`. Any CW filter (FIL1, FIL2, FIL3) counts as CW.
- **Callsigns:**
  - a candidate must follow the callsign formation rules (prefix, 1–4 digits, 1–4 letters; up to 6 letters for special-event calls with two or more digits, such as HA40EUDXF);
  - evidence for: after DE, after CQ / CQ TEST, before TEST, after TU, repeated;
  - `TEST X` or `X TEST` (a contest CQ) shows the station as calling CQ, like `CQ X`;
  - evidence against: before DE or before a report (5NN), because that is the station being called;
  - near variants (decodes with one error) support each other, and recent evidence wins;
  - stations are remembered by their real frequency, so moving the dial does not lose them. Signals more than 20 Hz apart are different stations: the two sides of a QSO are often only 20–30 Hz apart;
  - a QSO partner that is just a misread of the station's own call (`OE9EBI` next to `OE9IBI`) is not shown;
  - a station whose callsign is not known yet, within 100 Hz of a station in a QSO, is labelled as that QSO's other side (`IN3TWX>G3PQD`), since often only one side sends both calls.
- **Crosstalk:** a neighbour keying within 250 Hz mutes a channel only when its filter would really let that neighbour through, so close stations of any strength are decoded at once.
- **Pile-ups:** up to 12 stations at once (`chans`). The noise floor ignores narrow peaks, so it does not rise with many signals. In tests, 10 stations 150 Hz apart were all decoded.
- **Operators taking turns:** in a QSO or a contest run, two or more operators share one frequency, each with its own speed, weighting and spacing. Each channel keeps up to 3 timing profiles: when the level jumps or after a long silence, SKIM holds the first elements back and picks the operator that matches (level and speed), keeps the current timing if it still fits, or starts a new one.
- **Spaces between words:** SKIM measures each station's own gaps and splits them into element, letter and word gaps. When letter and word gaps are clearly apart, the word threshold goes between them. This handles keyers with heavy weighting or short word spaces (which would otherwise glue words together: "CQCQDE…") and slow or Farnsworth operators (which would otherwise be split letter by letter). When the gaps are not clear it uses the standard 5 units. It takes about 10 letters to adapt.
- **Word spacing fix (`words=1`):** what timing cannot settle (several operators alternating on one frequency, weak signals) is fixed on the finished line with a small CW vocabulary (CQ, DE, TEST, TU, 73, 5NN, RST, QTH, NAME, TNX…) and the callsign rules:
  - glued words are split only when every piece is a known word, a report, a number or a callsign, and there is a callsign or report among them, or enough typical CW words: `CQCQDEIK4PLU` → `CQ CQ DE IK4PLU`, `GM5NN73TUEE` → `GM 5NN 73 TU EE`, `TNXFERCALL` → `TNX FER CALL`, `CUAGNSN` → `CU AGN SN`;
  - `=` and `+` always stand apart: `TRIESTE=HW?` → `TRIESTE = HW?`;
  - right after `DR`, `NAME` or `QTH` (or `NAME IS`, `QTH IS`, `OP IS`, or `DR OM` after a greeting), one unknown word may follow, such as a name or a place: `GEDRALFON` → `GE DR ALFON`, `GEDROMALFONSO` → `GE DR OM ALFONSO`, `QTHISCADIZ` → `QTH IS CADIZ`;
  - with the word list (`SKIMDICT.BIN`: about 4,500 common English and 6,000 Spanish words, written for this project; accents removed and Ñ as N): two pieces that make a common word are joined when they are not both words (`TH ERE` → `THERE`, `PER HAPS` → `PERHAPS`, `GRA CIAS` → `GRACIAS`, `ANT ENA` → `ANTENA`), and a glued phrase is split into English, Spanish and CW words when it contains an unmistakable CW abbreviation such as HPE, TNX, QSO or QTH, or a number, or ends in a name, place or value after DR, NAME, QTH, NOMBRE or NR, or after RIG, PWR or WX followed by IS (`RIGISYAESUFT` → `RIG IS YAESUFT`, `PWRIS2TTW` → `PWR IS 2TTW`, `NRTRIESTE` → `NR TRIESTE`); numbers in cut figures (`2TTW` = 200 W, `58N`) count as numbers (`HPECUINPERSON` → `HPE CU IN PERSON`, `THEMEETINGSDRDEREK` → `THE MEETINGS DR DEREK`, `GRACIASPORELQSO` → `GRACIAS POR EL QSO`, `MINOMBREESPABLO` → `MI NOMBRE ES PABLO`). Words on the list are never split. In conversations many operators space words no wider than letters, or letters as wide as words, so timing alone cannot tell them apart;
  - a callsign cut in two is joined: `EA5 RJ` → `EA5RJ`;
  - callsigns already identified on the band, or repeated in the line, are preferred (`OS5ZK` next to `OS5Z` → `OS5Z K`);
  - it only moves spaces and never changes a letter. Plain-language words (`DEAR`, `TOUR`, `HIDE`, `COWBOYS`, `OKFRANCIS`) are left alone: over the 234,000 words of an English dictionary, 34 are split (all rare: GUDGEON, NONORNAMENTAL…). The callsign detection reads the fixed line.

## PSKReporter spots (experimental)

With `spot=1` in `SKIM.CFG`, SKIM reports the CW stations it decodes to [PSKReporter](https://pskreporter.info/), like a small skimmer. SKIM sends the reports itself, over the radio's Wi-Fi, in PSKReporter's own format ([developer information](https://pskreporter.info/pskdev.html)), with each station's frequency as SKIM shows it, to the hertz. (The firmware's `@SPOT` service is not used: it can only add a positive audio tone to the dial, which cannot place a CW station below the dial.)

To send spots the radio needs, besides `spot=1`:
- **MY CALL** set: it is the receiver call;
- a **position** (internal GPS or manual), from which the locator is taken. Until there is one, spots wait in a queue;
- a **Wi-Fi** network;
- **PSKREPORT on** for SKIM, on the DECODE screen, **SET**, when SKIM was started as a decoder mode (RTTY button). With it off, nothing is sent.

What is reported:
- only a station that names itself, with exactly the same letters, in **two different lines** (`CQ X`, `DE X`, `TEST X`, `X TEST` or `X X`). A misread call (`YP4MCSM` for `YP40CSM`) rarely repeats, so it is not spotted. Calls only guessed from a QSO next door are never spotted;
- each call **once an hour per band**, as PSKReporter asks;
- mode `CW`, the frequency in Hz (the true one with a transverter, from `@FREQ`), SKIM's own S/N estimate in dB, and the time from the radio's clock (PSKReporter corrects a clock that is off).

Reports are sent in one UDP datagram about every five minutes (sooner if 30 stations are waiting), plus one when SKIM stops, always from the same connection, as PSKReporter asks. SKIM shows up there as `SKIM 2026.11.24`.

The log shows every station queued and every datagram sent:

```
# spots: on, as EA5JLJ
# spot: IV3ZXF 7009.0 (18 dB)
# spots sent: 3 stations
```

or why nothing is sent (`# spots: off (set MY CALL on the radio)`, `# spots: waiting for a position (GPS or manual) on the radio`, `# spots: not sent, PSKREPORT is off for SKIM (DECODE > SET)`, `# spots: waiting, no network`).

Check the first spots on PSKReporter (*Show reception reports by* your call) against the `# spot:` lines in the log. Sending a datagram takes a moment: if the `# load:` line shows audio lost every five minutes, say so.

## Logs

`SKIMnn.LOG` (`SKIM01.LOG`, `SKIM02.LOG`…, one per session) starts with a header and logs the radio's mode code when it changes. Then it has one line per phrase:

```
# SKIM 2026-11-22 22:19 (started with the mode button: SKIM)
# radio mode: 32 dial 7.025.00
215416 7025.0 EA4XYZ>DL1ABC: DL1ABC GE 5NN  | 1921 DL1ABC GE 5NN
```

Each line is: time, the line as shown on DECODE, then after `|` the audio tone and the raw text.

The header also says whether the word list was loaded: `# word list: SKIMDICT.BIN, 31883 bytes`, `# word list: none (SKIMDICT.BIN not found)`, or `# word list: none (SKIMDICT.BIN damaged or too big)` (SKIM then works without it).

Once a minute it also writes `# load: 9% (8 channels), longest block: 12 ms, audio lost: 0, dial pauses: 2`: the share of real time the decoder needs; the longest time one 20 ms block of audio took (wall time, so it also counts moments when the radio's own tasks had the CPU); how many audio blocks were lost (should stay 0); and how many times decoding paused while the dial was turned.

Only the last 10 sessions are kept (setting `logs`): when SKIM starts it deletes the older logs, so the SD card does not fill up. The numbers go round from `SKIM99.LOG` back to `SKIM01.LOG`, so the newest log is not always the highest number: it is the one with the latest date in its header. Copy any log you want to keep to your computer. In the test logs an hour on the band took about 20 KB.

Mode codes measured on the IC-705: **32** = CW or CW-R, **48** = RTTY or RTTY-R, **80** = the decoder mode selected with the RTTY button. The low 4 bits are the filter (CW: 32 with FIL1, 33 with FIL2, 34 with FIL3), so SKIM only looks at the high 4 bits.

## Building

See **Building** in the [main README](../README.md#building) for the toolchain on Linux, macOS and Windows, and the versions it was tested with. In short: `arm-none-eabi-gcc`, Python 3, and the ICAP SDK of the **same release as your radio's firmware**, at `~/dev/icap-sdk` or in `$ICAP_SDK` (<https://github.com/sq6emm/icap-sdk>).

```bash
sh radio/build.sh          # build/SKIM.APP and build/icapsim
```

- **Build options:** the same as the SDK's reference decoders (Cortex-A9, NEON/VFPv3 hard-float, freestanding). The script fails if any external symbol is left unresolved.
- **Packaging:** the app is packed with `--abi 3 --background --mode SKIM:keep`. `:keep` leaves the radio's mode alone, so the key keeps working. With the default `usbd` the radio switches to USB-D and the key does nothing.
- **Size:** about 53 KB of code (the text handling is built for size, the decoder for speed), 6 KB of variables and a 16 KB stack, inside the 80 KiB app window. The decoder state (~157 KB with 12 channels), the station table (4 KB), the word list (32 KB, read from `SKIMDICT.BIN`) and the PSKReporter queue (3 KB) live in the app heap (`icap_heap()`), which is about 240 KB on the IC-705.

### Testing on a computer

`icapsim` is a small ICAP loader simulator. It feeds the app any WAV file as RX audio (see "Testing with your own audio" in the main README) and prints what would appear on the DECODE screen and in the panel.

```bash
mkdir -p /tmp/apps && cp radio/SKIM.CFG /tmp/apps/
radio/build/icapsim your_audio.wav -d /tmp/apps -m SKIM -p 30
```

## Known limitations

- The radio lists at most 8 apps; with more in `Apps`, SKIM may not appear (see Install).
- CW-R cannot be detected automatically; set `cwr=1`.
- After power-on SKIM has to be selected again with the RTTY button.
- Lines already written on DECODE cannot be changed. A callsign that is learned later shows up from the next line on.
- Very weak signals (below about −10 dB SNR in 2500 Hz) are not decoded reliably, and noise sometimes produces short garbage lines.
- In a busy 40 m contest SKIM used up to about 50 % of the CPU with 8 channels and lost no audio. That was measured before the noise-floor speed-up, which makes the decoder about 20–25 % faster in desktop tests. SKIM runs below the radio's own tasks, gives the CPU back after every 20 ms block of audio, and pauses while the dial is turned, but the radio can still feel slower while it decodes many stations. The log's `# load:` line shows the load every minute; if the radio feels slow or audio is lost, lower `chans`.

## Notices

- **Independent project.** SKIM is **not affiliated with, authorized by, or endorsed by** Icom Inc. or SQ6EMM.
- **Trademarks.** *Icom*, *IC-705* and *IC-9700* are trademarks of Icom Inc. They are used here only to identify compatible equipment.
- **No Icom code.** This project contains no Icom code or firmware and grants no rights to any Icom firmware. It only uses the public ICAP API (`icap.h`) from SQ6EMM's MIT-licensed SDK.
- **Third-party components.** The ICAP SDK and the custom firmware are SQ6EMM's work. Get them from their original source: <https://github.com/sq6emm>.
- **Not included.** This repository does not include Icom firmware, SQ6EMM's custom firmware, or the ICAP runtime files (`ICAPCOLD.BIN`, `ICAPSPOT.BIN`). Get them from SQ6EMM (<https://github.com/sq6emm>). Credits for the SDK parts used to build `SKIM.APP` are in [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md).
- **Use at your own risk.** SKIM is receive-only and never transmits, but using it requires modified radio firmware. Installing it may damage the radio and may void its warranty. The software is provided "as is", without warranty of any kind (see LICENSE). You are responsible for operating within your licence and your national regulations.

## Author and licence

Pablo Torres, **EA5JLJ**.

Released under the **MIT License**: see [LICENSE](../LICENSE). You may use, modify and share it, including in other projects, as long as the copyright notice is kept.
