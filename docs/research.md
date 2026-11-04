# Research: the valve-equipped groove production station (Korg Electribe EMX-1)

This document collects how the Korg ELECTRIBE·MX (EMX-1, 2003) works, as the reference for **Valvebox**, an original desktop instrument for research that reproduces its architecture, controls and behaviour. Valvebox doesn't use Korg's names, logos, panel artwork, samples or firmware. "Korg", "Electribe", "MMT" and "Valve Force" are trademarks of Korg Inc. and appear here only to identify the instrument being studied.

The main source is the EMX-1 Owner's Manual (99 pages, 2003). Page numbers below refer to it.

## 1. What it is

- A **groovebox**: a drum machine, five monophonic synths, three effects and a step sequencer in one box, played from the panel.
- **16 parts** (p.22, p.89):
  - nine drum parts, **1–5, 6A, 6B, 7A, 7B**;
  - five synth parts, **1–5**;
  - a **drum accent part** and a **synth accent part**.
- 6A/6B and 7A/7B are **exclusive pairs**: they can't sound together, and if both trigger on one step only B sounds (p.48). That's the open/closed hi-hat trick.
- **Memory:** 256 patterns (A.01–D.64) and 64 songs (S.01–S.64).
- **Effects:** three stereo processors, each with 16 types.
- **Sequences:** up to 128 steps per part, and up to 24 motion sequences per pattern.
- **Songs:** up to 256 positions, and 20,000 recorded events.
- **Valve Force:** two real **12AX7/ECC83 vacuum tubes** on the main and headphone outputs, driven by a TUBE GAIN knob separate from MASTER VOLUME (p.8, p.26). They sit in an oval window on the front. The glow is two yellow LEDs (Sound On Sound).
- **Look:** a striking **metallic-blue** all-metal panel.
  - 20 knobs, a slider, a ribbon controller and 68 keys.
  - The 16 step keys are laid out like a piano octave (white and black keys).
  - A backlit LCD. (Sound On Sound, Nov 2003)
- **Connections:**
  - Audio in (mono, with a MIC/LINE switch).
  - L/MONO and R outputs, individual outs 3/4, and headphones.
  - MIDI IN/OUT/THRU and a SmartMedia slot.
- **Size and weight:** 358 × 256 × 62 mm, 3.1 kg.

## 2. The panel (p.9–14)

### Common section

| Control | Function |
|---|---|
| MASTER VOLUME | Line-out and headphone level (not the individual outs) |
| TUBE GAIN | Level into the tubes. More is more distortion; normally at minimum. Line outs and headphones only |
| AUDIO IN LEVEL | Input level, with a meter and a CLIP indicator in the display |
| RESET / ERASE | Back to the start of the pattern or song. SHIFT + hold during playback erases the current part's triggers |
| REC | Record ready, or punch recording on and off during playback |
| STOP / CANCEL | Stops playback; cancels an operation |
| PLAY / PAUSE | Play; the key blinks while paused |
| TAP | Tap the tempo in quarter notes (twice or more), even while stopped |
| PART MUTE / REST·TIE | Hold + part key to mute it. In step recording it enters a rest, or a tie with a step key held |
| SOLO / TRANSPOSE | Hold + part key to solo (several allowed); press and release turns solo and mute off. Hold + dial or step keys to transpose ±24 (not saved, reset at power-on) |
| AUDIO IN THRU | Sends the input straight to the outputs |
| TEMPO LOCK | Keeps the tempo when changing patterns (not saved with a pattern; saved per song) |
| AUTO BPM SCAN | Detects the tempo of the audio input. Lit while scanning, blinks when it's found, press again to apply |
| WRITE / RENAME | Saves; SHIFT + it renames (8 characters) |
| BEAT LED | Blinks on quarter notes |
| Dial, cursor < > | Edit the value / move through the **matrix menu** of the current mode |
| Mode keys | PATTERN, PART EDIT, STEP EDIT, SONG, GLOBAL, MIDI |

### Edit section (p.11)

- **EFFECT:** TYPE (16), FX EDIT 1, FX EDIT 2, FX CHAIN (connections lit = active), EDIT SELECT (FX1/2/3) and MOTION SEQ (on/off).
- **PART COMMON:**
  - MOTION SEQ: off / SMOOTH / TRIG HOLD.
  - PITCH/GLIDE: pitch for drums, glide for synths.
  - EG TIME, PAN, LEVEL. LEVEL is the accent amount on an accent part.
  - AMP EG: decay or non-decay (gate).
  - ROLL, FX SELECT (FX1/2/3) and FX SEND.
- **MODULATION:** SPEED, DEPTH, TYPE, DEST and BPM SYNC.
- **SYNTH OSCILLATOR:** TYPE (16 models), OSC EDIT 1 and OSC EDIT 2 (synth parts only).
- **SYNTH FILTER:** CUTOFF, EG INT, RESONANCE, DRIVE and TYPE (LPF, HPF, BPF, BPF+). Synth parts only.

### Part select and step keys (p.12–13)

- **Part keys:**
  - Drum 1–5, 6A, 6B, 7A, 7B and drum ACCENT; synth 1–5 and synth ACCENT.
  - A drum key sounds its part at the accented level. SHIFT + key selects it silently.
- **KEYBOARD:** the 16 step keys play the selected synth part as a keyboard. The red select LEDs show the octave (8 ranges, from A-1..C1 up to A6..C8).
- **Select keys [<] [>]:** move the lower (red) row of select LEDs.
  - They show which bar the step keys edit, the keyboard octave, or the pattern-set group.
  - In Song mode they're fast-forward and rewind.
- **Select LEDs:** the upper (green) row shows the bar playing (1–8); the lower (red) row shows the bar being edited.
- **SHIFT + step key** runs the function printed under the key:

  | Key | Function |
  |---|---|
  | 1 | LAST STEP |
  | 2 | MOVE DATA (Move / Repeat / Random) |
  | 3 | SHIFT NOTE (Shift / Random) |
  | 4 | COPY PART |
  | 5 | COPY SOUND |
  | 6 | CLEAR MOTION |
  | 7 | CLEAR PART |
  | 8 | CLEAR PATTERN |
  | 9 | INSERT PATTERN (song) |
  | 10 | DELETE PATTERN (song) |
  | 11 | CLEAR SONG |
  | 12 | MIDI UTILITY |
  | 13 | CARD |

- **PATTERN SET:** hold it + a step key to jump to that key's pattern. There are 8 groups × 16 keys, with the groups on the select keys. SHIFT + PATTERN SET holds the mode, and holding two step keys sets up chain play.

### Arpeggiator (p.13, p.27–28)

- **Ribbon:** touching it plays the selected part.
  - On a synth part it plays 16th notes, with the gate time set by where you touch.
  - On a drum part, where you touch sets the note rate.
  - The notes retrigger each time you touch; with SHIFT held they don't.
- **Slider:** moves the pitch through the ARPEGGIO SCALE, around a stored **centre note**. It does nothing on drum parts.
- The notes to arpeggiate are the step keys held (KEYBOARD on).
- **31 scales:** Chromatic, Ionian, Dorian, Phrygian, Lydian, Mixolydian, Aeolian, Locrian, Major Blues, minor Blues, Diminished, Combination Diminished, Major/minor Pentatonic, three Ragas, Spanish, Gypsy, Arabian, Egyptian, Hawaiian, Pelog, Japanese (Miyakobushi), Ryukyu, Whole tone, m3rd, M3rd, 4th, 5th and Octave.
- **ARP CONTROL** (Global) can swap the ribbon and the slider.

### Display (p.15)

- An 8-character parameter display and a value display.
- PATTERN / SONG / WAVE indicators, the SmartMedia indicator and the input level meter with CLIP.
- A **knob indicator**: while a knob turns, the display shows its value, then goes back.
- An **original value indicator**: lit when a knob matches the stored value.
- **Part status rows** for all 16 parts: PLAY (not muted), SWING, ACCENT and OUTPUT L/R.

## 3. Sounds

### Drum parts (p.29–30)

| Parameter | Range | Notes |
|---|---|---|
| WAVE | 1–207 PCM drum waves | Kick 1–41; Snare 42–81; Rim 82–87; Clap 88–94; Hi-hat 95–120 (closed, half, open); Ride 121–126; Crash 127–130; Tom 131–137; Percussion 138–191; Synth perc 192–200; Reverse 201–204; Scratch 205–207 |
| PITCH | −63…+63 | ±2 octaves. ±39 = ±1 octave, ±6 = a semitone, with a stepped table in between |
| EG TIME | 0–127 | Decay time |
| PAN | L63–R63 | |
| LEVEL | 0–127 | |
| AMP EG | decay / gate | Decay fades over EG TIME; gate stops after EG TIME |
| ROLL, FX SELECT, FX SEND | | |
| Modulation SPEED | 0–127 | Or 8/1…1/64 with BPM SYNC |
| Modulation DEPTH | −63…+63 | |
| Modulation TYPE | Saw, Square, Triangle, Sample & Hold, EG | EG starts at maximum and decays. One type is free-running; the others reset their phase on each trigger |
| Modulation DEST | Pitch, Amp, Pan | |

### Synth parts (p.31–33)

- Each is a **monophonic** synth.
- **Glide** applies between legato notes: a gate longer than one step means no retrigger. It's off while ROLL is on.
- One **EG** (decay) drives the amp (AMP EG decay or gate) and the filter (EG INT ±63). There's no attack.
- **Filter:**
  - Types LPF, HPF, BPF, and **BPF+** (band-pass added to the dry signal).
  - CUTOFF, RESONANCE, and DRIVE (filter overdrive).
- **Modulation:** destinations Pitch, Amp, Pan, OSC EDIT 1, OSC EDIT 2 and Cutoff.
- **SYNTH TUNE:** ±50 cents per part.

### The 16 oscillator models (p.34–38)

| # | Model | WAVE | OSC EDIT 1 | OSC EDIT 2 |
|---|---|---|---|---|
| 1 | Waveform (2 osc) | Saw, Pulse, Tri, Sin | Waveshape (saw→square, pulse width, tri fold, sine harmonics) | Osc 2 pitch ±2 oct (47 = 1 oct; around 0 osc 1 only) |
| 2 | Dual osc | 20 combinations: osc 1 (Saw, Squ, Tri, Sin) × osc 2 (Saw, Squ, Tri, Sin, Noise) | Balance | Osc 2 pitch |
| 3 | Chord (4 osc) | Saw, Square, Tri, Sin | Chord: Major, 6th, 7th, M7, m7(♭5)…, 16 types | Voicing −3…+3 |
| 4 | Unison (6 osc) | 3–6 × Saw/Squ/Tri/Sin | Detune | Osc 1 pitch |
| 5 | Ring mod | 20 combinations | Mod depth (dry→ring) | Mod pitch |
| 6 | Osc sync | Saw, Pulse, Tri, Sin (slave) | Waveshape | Slave pitch, 0–127 (31 = +1 oct … 127 = +4 oct) |
| 7 | Cross mod (FM) | 20 combinations | Mod depth | Mod pitch |
| 8 | VPM (phase mod) | Saw, Squ, Tri, Sin carrier | Mod depth | Modulator harmonic 0.25…32 |
| 9 | Waveshaping | Type 1, Type 2 | Shape depth | Osc 2 pitch |
| 10 | Additive (3 osc) | Saw, Squ, Tri, Sin | Osc 2 harmonic 0.25–32 | Osc 3 harmonic 0.25–32 |
| 11 | Comb | Saw, Squ, Tri, Sin, Noise | Feedback | Comb pitch 0–127 (+4 oct) |
| 12 | Formant | — | Formant width (vowels) | Formant shift ±63 |
| 13 | Noise | — | Rate (sample-and-hold rate) | Colour: LPF ↔ HPF ±63 |
| 14 | PCM + comb | 76 PCM waves | Feedback | Comb pitch |
| 15 | PCM + waveshape | 76 PCM waves | Shape depth | Character |
| 16 | Audio in + comb | — | Feedback | Comb pitch |

**PCM waves (76):**
- 1–5 keyboard, 6–8 mallet, 9–11 woodwind, 12–14 brass, 15–17 choir, 18–20 strings, 21–25 guitar, 26–30 bass.
- 31–53 synth waves, 54–55 motion, 56–65 hits.
- 66–72 **chord splits**: several chords across the keyboard.
- 73–76 **drum sets**: the drum waves across the keys.

### Effects (p.41–45)

- Every part sends to **one** processor (FX SELECT) if FX SEND is on.
- A part on output bus 3/4 bypasses the effects.
- **FX CHAIN** feeds one processor into another. The options are no chain, 1→2, 2→3 and 1→2→3.
- Groups: **stereo** (chorus/flanger, phaser, ring mod, compressor, decimator, EQ, LPF, HPF), **mono-mix** (reverb, mod delay, grain shifter, talking mod, pitch shifter, distortion) and **stereo-cross** (BPM sync delay, short delay).

| Type | EDIT 1 | EDIT 2 |
|---|---|---|
| Reverb | Time | Level |
| BPM sync delay | Time, 1/64…1/1 note | Depth (level + feedback) |
| Short delay | Time | Depth |
| Mod delay | Time, 1/64…1/1 note | Depth |
| Grain shifter | Speed (grain interval in steps, from a table) | Balance |
| Chorus/flanger | Speed | Depth (chorus → flanger) |
| Phaser | Speed | Depth |
| Ring mod | Oscillator frequency | Balance |
| Talking mod | Formant a-e-i-o-u | Offset ±63 |
| Pitch shifter | ±2400 cents | Balance |
| Compressor | Sensitivity | Attack |
| Distortion | Gain | Level |
| Decimator | Sample rate | Bit depth |
| EQ | Low gain ±63 | High gain ±63 |
| LPF | Cutoff | Resonance |
| HPF | Cutoff | Resonance |

## 4. Sequencer

- **Patterns:**
  - Name (8 characters), TEMPO 20.0–300.0 (SHIFT + dial for tenths), SWING 50–75 % on the even steps, LENGTH 1–8 bars.
  - BEAT: 16 (16ths), 32 (32nds), 8Tri (8th triplets, 12 per bar) or 16Tri (16th triplets, 12 per bar).
  - LAST STEP 1–16, for odd time signatures. It masks rather than deletes.
  - ROLL TYPE 2/3/4 hits per step; ARP SCALE.
  - Per-part ACCENT SW, SWING SW and OUTPUT BUS (L/R or 3/4). Clear Pattern turns them all on and sets the bus to L/R.
  - Swing is ignored for triplet beats.
- **Changing patterns** during playback waits for the end of the current pattern. The next name blinks.
- **Synth steps:** trigger, note (C-1…G9) and gate time (0.25…128 steps). A gate longer than the step means a legato next note: no retrigger of oscillator, EG or modulation, and glide applies.
- **Accent:**
  - Steps on the accent part add volume to the parts with ACCENT SW on.
  - LEVEL on the accent part sets how much (0 = no difference). The diagram shows velocity 100 for unaccented notes and up to 127 for accented ones.
  - A pressed part key always plays at the accented level.
- **Recording:**
  - *Realtime:* REC then PLAY, then play part keys or the keyboard. REC again ends recording but keeps playing.
  - *Step recording:* STEP EDIT > Step Rec. Each note advances the target step; REST/TIE enters a rest, or a tie with the key held.
  - *Erase:* SHIFT + hold RESET/ERASE removes the triggers that pass.
- **Editing:**
  - Drum triggers are toggled on the step keys, bar by bar with the select keys.
  - STEP EDIT: NoteNo, GateTime, MotDest, MotValue.
  - **Clear Part:** synth steps reset to C4 with gate 0.75.
- **Motion sequence:**
  - It records the knobs and keys of a part (or FX EDIT 1/2) for **one pass** of the pattern, then REC turns itself off.
  - It plays back in **SMOOTH** (continuous) or **TRIG HOLD** (each note holds the value from its trigger) mode.
  - Effects always play smooth and accents always trig-hold. Switch-type parameters (AMP EG, ROLL, FX SELECT/SEND, MOD TYPE/DEST, BPM SYNC, FILTER TYPE) always trig-hold.
  - The limit is 24 per pattern. SHIFT + MOTION SEQ shows what's recorded ("FreeMot").
- **Compare:** SHIFT + PATTERN plays the stored version of the pattern.
- **Write:**
  - WRITE then dial a destination then WRITE.
  - Patterns, songs and edits are lost unless written.
  - **Memory Protect** is on from the factory.

## 5. Songs (p.65–73)

- Each position holds a pattern (or End) and a NOTE OFFSET of ±24 for the synth parts.
- Insert, delete and clear (SHIFT + 9/10/11).
- **MUTE HOLD:** keeps the mutes when the pattern changes.
- **NEXT SONG:** chains songs.
- **TEMPO LOCK** is stored per song.
- **Event recording** records drum keys, keyboard, arpeggiator, knobs and keys (selected part), solo and mute, and tempo. Each pass replaces only the same kind of event, and events play back smooth.
- The select keys fast-forward and rewind.

## 6. Global and MIDI (p.75–85)

- **Global:**
  - METRONOME: Off, Rec0, Rec1 (a 1-bar count-in), Rec2, On, or On→3/4.
  - MASTER TUNE ±50 cents (not the drums), PROTECT on/off, ARP CONTROL Normal or Reverse.
  - SHIFT + 13 is the card menu: format, save all, load all, load one pattern, delete.
- **MIDI:**
  - Channel per synth part (defaults 1–5; part 1 is also the global channel) and one for the drums (10).
  - CLOCK Int, Ext or Auto. BEND RANGE ±12.
  - MIDI UTILITY (SHIFT + 12): filter P/C/E/N (program, control, exclusive, notes), drum note numbers, CC assignment per knob, and data dump.
  - Patterns switch on program change.
- **Drum note defaults:** 1 = C2, 2 = D2, 3 = E2, 4 = F2, 5 = G2, 6A = F♯2, 6B = A♯2, 7A = C♯3, 7B = D♯3.
- **Synth CC defaults (p.92):** OSC TYPE 70, OSC EDIT 1 14, OSC EDIT 2 15, GLIDE 5, FILTER TYPE 83, CUTOFF 74, RESONANCE 71, EG INT 79, DRIVE 84, LEVEL 7, PAN 10, EG TIME 75, AMP EG 86, ROLL 85, FX SEND 91, FX SELECT 81, MOD TYPE 87, MOD DEPTH 90, MOD SPEED 89, MOD DEST 88, BPM SYNC 82, MOTION SEQ 80.
- **Effect CC defaults:** FX1 type/edit 1/edit 2/motion = 12/92/93/20, FX2 = 13/94/95/21, FX3 = 24/25/26/22, FX CHAIN 23.

## 7. Decisions for the reproduction

1. **Panel logic as a headless state machine** (core/Machine): every key, knob, dial move, mode and matrix page, with LEDs and display text as outputs. The tests drive it exactly as a player would.
2. **No Korg samples.** The 207 drum waves and 76 PCM oscillator waves are **synthesized by Valvebox** (modal and noise drum models; additive, FM and plucked-string models for the instruments). They keep the same numbering ranges and categories, so "waves 95–120 are hi-hats" still holds. They're our own sounds with our own names.
3. **Oscillator models:** band-limited where it matters (polyBLEP), with all 16 models and their two edit parameters as described.
4. **Valve stage:** a 12AX7-style triode model (asymmetric soft clipping, so even harmonics) in front of MASTER VOLUME. More TUBE GAIN gives more drive and more level, on the main outputs only.
5. **SmartMedia becomes files:** Save All, Load All, Load one pattern and Delete use files on disk. MIDI data dumps become the same file format.
6. **Holding keys with a mouse:** SHIFT, PART MUTE, SOLO and PATTERN SET can be held from the computer keyboard, or latched with a click (lit until the next key). A pointer can't press two panel keys at once.
7. **Factory content:** our own factory patterns and demo songs, not the original's 192 patterns.

## Sources

- KORG ELECTRIBE·MX EMX-1 Owner's Manual (2003), [archive.org copy](https://archive.org/download/manualsonline-id-862c359b-6a23-4bc3-b7d0-b372b1465025/862c359b-6a23-4bc3-b7d0-b372b1465025.pdf); also on [ManualsLib](https://www.manualslib.com/manual/90187/Korg-Emx-1.html).
- [Sound On Sound review, November 2003](https://www.soundonsound.com/reviews/korg-electribe-emx1): appearance, the valve window, chain options, criticisms.
- [Vintage Synth Explorer](https://www.vintagesynth.com/korg/electribe-mx-emx-1), [Sonic State](https://sonicstate.com/synth/korg_emx1/), [MusicRadar EMX-1 SD review](https://www.musicradar.com/reviews/tech/korg-electribe-esx-1-sd-emx-1-sd-278116).
