## Midi implementation

- [Receiving Pressure and Modwheel data](#receiving-pressure-and-modwheel-data)
- [Midi Tuning Standard](#midi-tuning-standard)
- [Y/Z Midi Controller](#yz-midi-controller)
- [MPE implementation](#mpe-implementation)
- [Parameter CC Table](#parameter-cc-table)
- [CC Lookup Table](#cc-lookup-table)
- [Channel Mode Messages](#channel-mode-messages)

---

### Receiving Pressure and Modwheel data
Midi Pressure (which is the same as Aftertouch) can be received in three ways, depending on the [System Setting](settings_menu.md#aftertouch-type-voice-bend-range):
- Channel Aftertouch on the regular midi channel (applied to all strings)
- Polyphonic Aftertouch on the regular midi channel (applied to the string playing the indicated note)
- Channel Aftertouch on an MPE Member Channel (applied to the string linked to that channel)

Modwheel data can also be received in three ways:
- On the regular Midi Channel (applied to all strings)
- On an MPE Manager Channel (applied to all string in the MPE Zone)
- On an MPE Member Channel (applied to the string linked to that channel)

Whenever Plinky generates a Midi-generated voice for a string, it picks the higher of the two values on that string and uses that as the Synth Pressure value.

---

### Midi Tuning Standard
Plinky supports *Bulk Tuning Dump*, *Single-note Tuning Change* and *Scale Octave Tuning* (1 and 2 byte forms) as defined in the [Midi Tuning Specification](https://midi.org/midi-tuning-updated-specification). The tuning changes will always be applied in real-time, regardless of whether the command is marked as real-time or non real-time.

**Important:** 

- Midi Tuning can be globally enabled/disabled in the [Settings Menu](settings_menu.md#enable-midi-tuning) and is disabled by default
- Midi Tuning pitches are affected by the [A4 Reference Pitch](settings_menu.md#a4-reference-pitch)

Behavior when Midi Tuning is enabled:

##### Physical Touches
- Each pad represents a certain semitone. When [P_SCALE] is set to a microtonal scale, it is the semitone closest to the pitch on the pad
- For the two pads closest to your touch, Plinky replaces its semitone by the Midi Tuning pitch (if available)
- The position of your touch between the two pads is the interpolation factor
- The resulting pitch is the interpolation between the two pad pitches, using the interpolation factor and [P_MICROTONE]

##### Midi Touches
- Plinky replaces the semitone of the Note On message by the Midi Tuning pitch (if available)
- Pitchbend is received as a separate message and is applied as a straight pitch offset

##### CV Touches
###### CV Quantize = Scale
The CV Pitch is mapped to the pads:

- Plinky finds the two pads whose pitches are closest to the CV Pitch
- Plinky calculates the interpolation factor from the distance between the CV Pitch and the pitches of those pads
- With these pads and the interpolation factor, the Midi Tuning is applied as if this were a physical touch
###### CV Quantize = Chromatic
Same as above, but as if [P_SCALE] were set to Chromatic
###### CV Quantize = Off
The CV Pitch is used exactly as it came in, Midi Tuning has no effect

---

### Y/Z Midi Controller
Plinky can be used as a generic midi controller. In this mode the position (Y) and pressure (Z) of each of the eight strings are sent as CC values. Positions on CC 32-39 and pressures on CC 40-47. These values come directly from the touchplate - they are not affected by any parameters and do not use the synth engine. The Y/Z Controller mode can be enabled in [Midi out Filter 2](settings_menu.md#filter-2)

---

### MPE implementation
#### Usage
By default Plinky defines the lower zone with manager channel 1 and strings 1-8 using Midi channels 2-9. Using one zone with eight member channels is generally the recommended setup, but different setups with one or two zones can be used if so desired.

Plinky uses per-voice channel pressure and pitchbend by default. Many MPE controllers send CC74 as a third parameter, which controls the Sustain Level of Envelope 1 in Plinky and therefor modulates the level and brightness of sustained notes.

On top of these defaults, any parameter that has a CC in the [CC Table](#parameter-cc-table) can be set by sending it on an MPE member channel. If the parameter is multi-timbral, it will be set for that voice only. Otherwise, the parameter will be set globally.

#### Setup
Use the [Settings Menu](settings_menu.md) to enable MPE input and/or output, as well as to set up the channels for one MPE zone.

As prescribed by the [MPE specification](https://midi.org/mpe-midi-polyphonic-expression), two zones can be set up by using MPE Configuration Messages. An MPE Configuration Message consists of the following four CC messages:

| | | |
|---|---|---|
| **CC 101 0** | RPN MSB | MPE Configuration Message |
| **CC 100 6** | RPN LSB | MPE Configuration Message |
| **CC 6 N** | Data Entry MSB | N = Zone channel count (1-15, 0 = off) |
| **CC 38 0** | Data Entry LSB | Value unused but needs to be sent for a complete RPN value |

These CCs should be sent on the manager channel of the desired MPE zone (1 for lower, 16 for upper).

#### Channel to String mapping (8 channels or less)
- The lower zone starts mapping Midi Channel 2 to String 1 and works its way upwards
- The upper zone starts mapping Midi Channel 15 to String 8 and works its way downwards  

*Example: The lower zone with 4 channels maps Midi Channels 2-5 to Strings 1-4*

#### Channel to String mapping (more than 8 channels)
- Each Zone uses half as many Strings as Midi Channels
- The first half of the Channels map as above
- After that, Channels roll over to fit within the available strings

*Example:*

- *The upper zone has 11 Midi Channels*
- *The zone gets 6 Strings (11/2, rounded up)*
- *Midi Channels 5-10 map to Strings 3-8*
- *Midi Channels 6-15 roll over and map to Strings 3-7*

---

### Parameter CC Table
| Section | Parameter | CC | | Section | Parameter | CC | | Section | Parameter | CC |
|---------|-----|-----|-|---------|-----|-----|-|---------|-----|-----|
| **General** | Pattern ID<br>Sample ID<br>Latch Toggle<br>HPF<br>Synth Level<br>Input Level<br>Synth Wet/Dry<br>in Wet/Dry | 83<br>82<br>69<br>31<br>7<br>89<br>8<br>90 | | **Sound** | Shape<br>Noise<br>Distortion<br>Resonance<br>Pitch<br>Glide<br>Interval | 13<br>2<br>4<br>71<br>9<br>5<br>14 | | **Sampler** | Scrub<br>Scrub Jitter<br>Grain Size<br>Size Jitter<br>Play Speed<br>Speed Jitter<br>Timestretch | 15<br>116<br>16<br>117<br>17<br>118<br>18 |
| **Env 1** | Sensitivity<br>Attack 1<br>Decay 1<br>Sustain 1<br>Release 1 | 3<br>73<br>75<br>74<br>72 | | **Arp** | Arp Toggle<br>Arp Order<br>Arp Clock Div<br>Arp Chance<br>Arp Euclid Length<br>Arp Octaves | 102<br>103<br>104<br>105<br>106<br>107 | | **Delay** | Delay Send<br>Delay Time<br>Delay Ping Pong<br>Delay Wobble<br>Delay Feedback | 94<br>12<br>112<br>113<br>95 |
| **Env 2** | Env 2 Level<br>Attack 2<br>Decay 2<br>Sustain 2<br>Release 2 | 19<br>20<br>21<br>22<br>23 | | **Seq** | Seq Order<br>Seq Clock Div<br>Seq Chance<br>Seq Euclid Length<br>Gate Length<br>Step Offset | 108<br>109<br>110<br>111<br>11<br>85 | | **Reverb** | Reverb Send<br>Reverb Time<br>Reverb Shimmer<br>Reverb Wobble | 91<br>92<br>93<br>114 |
| **LFO A** | LFO A Rate<br>LFO A Depth<br>LFO A Offset | 24<br>25<br>26 | | **LFO X** | LFO X Rate<br>LFO X Depth<br>LFO X Offset | 76<br>77<br>78 | | | | |
| **LFO B** | LFO B Rate<br>LFO B Depth<br>LFO B Offset | 27<br>28<br>29 | | **LFO Y** | LFO Y Rate<br>LFO Y Depth<br>LFO Y Offset | 79<br>80<br>81 | | | | |

---

### CC Lookup Table
|     | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|-----|---|---|---|---|---|---|---|---|
| **0** | - | *[Mod Wheel]* | Noise | Sensitivity | Distortion | Glide | *[Data MSB]* | Synth Level |
| **8** | Synth Wet/Dry | Pitch | - | Gate Length | Delay Time | Shape | Interval | Scrub |
| **16** | Grain Size | Play Speed | Timestretch | Env 2 Level | Attack 2 | Decay 2 | Sustain 2 | Release 2 |
| **24** | LFO A Rate | LFO A Depth | LFO A Offset | LFO B Rate | LFO B Depth | LFO B Offset | - | HPF |
| **32-63** | - | - | *[Reserved* | *for* | *CC14* | *LSB]* | - | - |
| **64** | *[Sustain]* | - | *[Sostenuto]* | - | - | Latch Toggle | - | Resonance |
| **72** | Release 1 | Attack 1 | Sustain 1 | Decay 1 | LFO X Rate | LFO X Depth | LFO X Offset | LFO Y Rate |
| **80** | LFO Y Depth | LFO Y Offset | Sample ID | Pattern ID | - | Step Offset | - | - |
| **88** | - | Input Level | in Wet/Dry | Reverb Send | Reverb Time | Reverb Shimmer | Delay Send | Delay Feedback |
| **96** | *[Data Increment]* | *[Data Decrement]* | *[NRPN LSB]* | *[NRPN MSB]* | *[RPN LSB]* | *[RPN MSB]* | Arp Toggle | Arp Order |
| **104** | Arp Clock Div | Arp Chance | Arp Euclid Length | Arp Octaves | Seq Order | Seq Clock Div | Seq Chance | Seq Euclid Length |
| **112** | Delay Ping Pong | Delay Wobble | Reverb Wobble | - | Scrub Jitter | Size Jitter | Speed Jitter | - |
| **120** | *[All Sound Off]* | *[Reset All Ctrl]* | *[Local Control]* | *[All Notes Off]* | *[Omni Mode Off]* | *[Omni Mode On]* | *[Mono Mode On]* | *[Poly Mode On]* |

*Add row and column numbers to get the CC number*

---

### Channel Mode Messages
##### MPE in Off
- Executes global actions plus per-string actions on all strings
##### MPE in On
- Received on manager channel: Executes global actions plus per-string actions on all strings in the selected zone
- Received on member channel: Executes per-string action on respective string

| CC number |  Message | CC value |Result |
|-|-|-|-|
| 120 | All Sound Off | Ignored | Global: Clears effects<br>Per string: Lifts sustain, ends midi note, clears voice |
| 121 | Reset All Controllers | Ignored | Global: Resets channel aftertouch, pitchbend<br>Per string: Resets mod wheel, sustain, pitchbend, aftertouch, NRPN number, RPN number|
| 122 | Local Control | 0-63: Off<br>64-127: On | Global: When local control is off, the touchpads don't generate synth notes |
| 123 | All Notes Off | Ignored | Per string: Lifts sustain, ends midi note |
| 124-127 | Mode | N/A | *Not implemented. Plinky is always in Mode 3: Omni Mode off, Poly Mode on* |