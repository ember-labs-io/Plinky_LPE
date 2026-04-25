## Settings Menu

### Navigation
Open the Settings Menu by holding [FN_SHIFT_A] or [FN_SHIFT_B] and pressing [P_SETTINGS1]

In the settings menu the [I_RIGHT] icon means rotating the encoder changes the selected setting. Press the encoder once to switch to value editing: the [I_LEFT] icon shows and rotating the encoder changes the value of the selected setting. Pressing the encoder again switches back to changing the selected setting.

It is also possible to press any of the lit up touchpads to select a setting

### Overview
| Section | Settings |  |  |  |  |  |  |  |
|-|-|-|-|-|-|-|-|-|
| [**System**](#system) | [Global Pad Layout](#global-pad-layout) | [Accelerometer Sensitivity](#accelerometer-sensitivity) | [Encoder Direction](#encoder-direction) | [A4 Reference Pitch](#a4-reference-pitch) | [Enable Midi Tuning](#enable-midi-tuning) | [Enable Local Control](#enable-local-control) | | |
| [**Midi in**](#midi-in) | [In Channel(s)](#in-channels) | [Enable MPE In](#enable-mpe-in) | [Velocity / Pressure In Balance](#velocity-pressure-in-balance) | [Aftertouch Type / Voice Bend Range](#aftertouch-type-voice-bend-range) | [Channel Bend-Range](#channel-bend-range) | [Map To Scale](#map-to-scale) | [Clock Multiplier](#clock-multiplier) | [Filter](#filter) |
| [**Midi out**](#midi-out) | [Out Channel(s)](#out-channels) | [Enable MPE Out](#enable-mpe-out) | [Velocity / Pressure Out Balance](#velocity-pressure-out-balance) | [Aftertouch Type](#aftertouch-type) | [Enable TRS Out](#enable-trs-out) | [Enable Soft Thru](#enable-soft-thru) | [Filter 1](#filter-1) | [Filter 2](#filter-2) |
| [**CV**](#cv) | [CV In Quantization](#cv-in-quantization) | [Gate In Pressure/Gate](#gate-in-pressuregate) | [Clock In PPQN](#clock-in-ppqn) | [Clock Out PPQN](#clock-out-ppqn) | | | | |
| [**Actions**](#actions) | [Reboot](#reboot) | [Touch Calib](#touch-calib) | [CV Calib](#cv-calib) | [Push Preset](#push-preset) | [OG Presets](#og-presets) | [Midi Panic](#midi-panic) | [System Reset](#system-reset) | |

---

#### System

##### Global Pad Layout
*Options: Preset / Global (default: Preset)*

The [P_ROOT], [P_OCT], [P_SCALE] and [P_COLUMN] parameters define the pad layout

- Preset: The layout parameters are saved and recalled per preset
- Global: The layout parameters persist across presets
##### Accelerometer Sensitivity
*Range: -200% to 200% (default: 100%)* 

Scales how strongly the X/Y accelerometers affect the X/Y lfos. Setting this to 0% turns off the accelerometers altogether.
##### Encoder Direction
*Options: Normal / Invert (default: Normal)*

Allows inverted encoder behavior in case an inverted encoder was used in the hardware.
##### A4 Reference Pitch
*Range: 430Hz to 445Hz (default: 440Hz)*

Offsets the tuning center of the entire device.
This includes offsetting microtonal scales loaded through the [Midi Tuning Standard](midi_implementation.md#midi-tuning-standard).
##### Enable Midi Tuning
Enables whether microtonal scales sent through the [Midi Tuning Standard](midi_implementation.md#midi-tuning-standard) are applied to Plinky's output.  
Receiving [Midi Tuning Standard](midi_implementation.md#midi-tuning-standard) data is still possible when Enable Midi Tuning is off.
##### Enable Local Control
When this is off, touching Plinky's touchplate will not generate any audio. It effectively turns the Plinky into a sound module.  
[Y/Z controller mode](midi_implementation.md#yz-midi-controller) still works when Enable Local Control is off.

---

#### Midi in

##### In Channel(s)
###### MPE off
*Options: 1 - 16 (default: 1)*

Sets the channel Plinky receives midi on
###### MPE on
*Options: 2 [1] through 2-16 [1], and 15 [16] through 2-15 [16] (default: 2-9 [1])*

Sets either the lower or upper MPE zone - indicated by whether the manager channel is [1] or [16] - and which channels the zone uses. The voice channels start at 2 and count up for the lower zone, or start at channel 15 and count down for the upper zone.

*Note that MPE Channels are a shared setting for Midi input and output. Changing them in the Midi in section also changes them in the Midi out section, and vice versa.*
##### Enable MPE In
Enables receiving MPE
##### Velocity / Pressure In Balance
*Range: 100/0 to 0/100. (default: 50/50)*

Allows tweaking how strong the velocity and pressure midi data affect the pressure of the generated Plinky-touch

- If this is set to 100/0, an incoming note with velocity 127 will lead to a touch with 100% pressure. Incoming aftertouch data will have no effect
- If this is set to 80/20, an incoming note with velocity 127 will lead to a touch with 80% pressure. Incoming aftertouch data is able to affect the remaining 20%, to the point where aftertouch with level 127 brings the Plinky touch pressure up to 100%

This goes for all settings: the note velocity can create start-pressures up to the velocity value. Aftertouch can then create additional pressure up to the pressure value. When the note velocity and aftertouch values are both 127, the resulting pressure is always 100%

It's not necessary to understand the math for this setting to be useful. In practice it helps to generate touches with a workable pressure with a variety of midi controllers. If you have a midi keyboard with no aftertouch, setting this to 100/0 will still allow you to generate touches with full pressure. If you have an aftertouch or mpe controller, this setting can be tweaked to your liking
##### Aftertouch Type / Voice Bend Range
###### MPE off - Aftertouch Type
*Options: Off / Mono / Poly (default: Mono)*

Sets whether Plinky receives no aftertouch ("Off"), channel aftertouch ("Mono"), or poly aftertouch ("Poly")
###### MPE on - Voice Bend Range
*Options: 1, 2, 7, 12, 14, 24, 48, 96 (default: 48)*

Sets the range of the MPE per-voice pitchbend. The global default for MPE is 48
##### Channel Bend-Range
*Options: 1, 2, 7, 12, 14, 24, 48, 96 (default: 2)*

Sets the range of the channel pitchbend.
##### Map To Scale
Sets whether received Note On messages map to scale notes.
##### Clock Multiplier
*Options: 1/2x / 1x / 2x (default: 1x)*

Allows dividing or multiplying the incoming midi clock.
##### Filter
| | | |
|-|-|-|
|[I_TEMPO] | Clock | Enable receiving midi clock |
|[I_PLAY] | Sync | Enable receiving midi transport (start/stop/continue) |
|[I_KNOB] | CC | Enable receiving CCs to set Plinky parameters |
|[I_CC14] | CC14 | Enable receiving CCs to set Plinky parameters, of which CCs under CC64 are interpreted as 14bit values |

---

#### Midi out

##### Out Channel(s)
See description for Midi In Channel(s)
##### Enable MPE Out
Enables sending MPE

**Important:** This setting will change how physical touches and incoming CV pitch are processed:

- MPE Out Off: When a pitch caused by a physical touch or a CV pitch slides from one note to the next, Plinky processes this as a different note. The note name on the diplay updates to the new note and Midi will send a Note Off for the old note and a Note On for the new note.
- MPE Out On: When a pitch caused by a physical touch or a CV pitch slides from one note to the next, this is processed as a pitch slide relative to the same starting note. The note name stays the same and no new note is output over Midi. Instead, Midi sends new MPE pitchbend values to represent the new pitch.
##### Velocity / Pressure Out Balance
This setting works like Midi in Velocity / Pressure Balance, but in reverse. It takes the pressure of a Plinky touch and splits it into Note On velocity and Aftertouch pressure. It can be used to tweak how external synths respond to the pressure value of Plinky touches.

When the input and output Vel/Pres Balance are the same, outgoing velocity and aftertouch will be identical to the velocity and aftertouch that came in.
##### Aftertouch Type
*Options: Off / Mono / Poly (default: Mono)*

Defines whether Plinky sends no aftertouch ("Off"), sends the max pressure over channel aftertouch ("Mono"), or sends per-string pressure over poly aftertouch ("Poly")
##### Enable TRS Out
Plinky sends identical Midi data over USB and the 3.5mm TRS Midi connector. The TRS connection is much slower than USB, which means all Midi out data needs to be throttled. Turning off TRS means this throttling is no longer needed and much higher data speeds can be achieved over USB.
##### Enable Soft Thru
Incoming Midi that is not on any of Plinky's selected input and output channels will be sent to Midi out
##### Filter 1
| | | |
|-|-|-|
| [I_TEMPO] | Clock | Enable sending Midi clock |
| [I_PLAY] | Sync | Enbable sending Midi transport (start/stop/continue) |
| [I_KNOB] | CC | Enable sending Plinky parameters as CCs |
| [I_NRPN] | NRPN | Enable sending Plinky parameters as NRPNs |

##### Filter 2
| | | |
|-|-|-|
| [I_MPE_FINE] | MPE Fine Tuning | When MPE is on, per voice pitchbend messages will include modulations from the [P_PITCH] and [P_GLIDE] parameters |
| [I_ALFO] | LFO out | Enable sending the LFO values on CCs 48, 49, 50 and 51 |
| [I_YZ] | Y/Z Controller | Enable sending the touch values for strings 1-8 on CCs 32-39 (position) and 40-47 (pressure) respectively |

---

#### CV

##### CV In Quantization
*Options: Off, Chromatic, Scale (default: Off)*

The Chromatic and Scale options round the incoming cv pitch towards the goal pitch, which is the closest semitone (Chromatic) or scale note (Scale)

The [P_MICROTONE] parameter defines how strong this rounding is:

- Microtone 0: fully rounded to the goal pitch
- Microtone 20: the detuning is scaled down to a maximum of 20% between the goal pitch and the next semitone/scale note
- Microtone 100: no rounding applied at all
##### Gate In Pressure/Gate
*Options: Gate, Pressure (default: Gate)*

Defines whether the incoming CV Gate voltage is processed as a binary or continuous value
##### Clock In PPQN
*Options: 1, 2, 4, 8, 16, 24, 48 (default: 4)*

Defines the pulses per quarter note of the incoming clock
##### Clock Out PPQN
*Options: 1, 2, 4, 8, 16, 24, 48 (default: 4)*

Defines the pulses per quarter note of the outgoing clock

---

#### Actions
Hold down the encoder to trigger an action
##### Reboot
Reboot Plinky
##### Touch Calib
Start the touch calibration process
##### CV Calib
Start the CV calibration process
##### Push Preset
Output all parameters of the current preset over midi. Uses NRPN if enabled in [Midi Out Filter 1](#filter-1)
##### OG Presets
Reverts system settings and all presets on Plinky to be compatible with the OG firmware
##### Midi Panic
Clears all touches on the Plinky and sends out a Midi Panic message
##### System Reset
- Sets all system settings to default values
- Sets the global layout parameters to default values
- Sets all multi-timbral parameters to [mono-timbral editing mode](v0.5.0.md#multi-timbral-parameters)
- Clears saved [midi tuning](midi_implementation.md#midi-tuning-standard) data
