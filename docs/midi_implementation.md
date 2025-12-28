# Plinky LPE - MIDI Implementation

## Quick Links
1. [Using 14 bit CCs for parameters](#1-using-14-bit-ccs-for-parameters)
2. [Midi CC Table](#2-midi-cc-table)
3. [Midi CC Table (Reverse Lookup)](#3-midi-cc-table-reverse-lookup)
4. [NRPN Protocol](#4-nrpn-protocol)
5. [NRPN Table](#5-nrpn-table)

## 1. Using 14 bit CCs for parameters
All parameter CCs under 32 can be used as 14 bit CCs with an effective range of 0-16383. The listed CC acts as coarse control, while the listed CC + 32 acts as fine control

> For example: The Noise parameter can be controlled with CC 2 (coarse) and CC 34 (fine)

### Enabling 14 bit CCs for parameters
By default Plinky will process all midi CCs as regular 7 bit controls. This means sending value 127 to CC 2 will set the Noise parameter to 100.0%

>**As soon as Plinky receives its first fine control CC, it will enable 14 bit controls for all parameter CCs under 32**


With **14 bit CCs on**, all fine control values default to zero. This means sending value 127 to CC 2 will set the Noise coarse value to 127 while the fine control is still at 0. This maps to a parameter value of roughly 99.2%. After also sending value 127 to CC 34, both the coarse and fine controls are set to 127, which maps to 100.0%

| Mode | Coarse CC sent | Fine CC sent | 14 bit value | Param value |
|-|-|-|-|-|
| **14 bit CCs off** | 127 | N/A | N/A | 100.0% |
| **14 bit CCs on** | 127 | 0 or nothing | 16256 |~99.2% |
| **14 bit CCs on** | 127 | 127 | 16383 | 100.0% |
| | | | |

> Plinky will reset to **14 bit CCs off** on reboot

> Enabling 14 bit CCs does not affect existing parameter values

> This entire section does not apply to reserved CC numbers, like Mod Wheel and NRPN Data Entry

---

## 2. Midi CC Table

| Section | Parameter | CC | -- | Section | Parameter | CC | -- | Section | Parameter | CC |
|---------|-----|-----|-|---------|-----|-----|-|---------|-----|-----|
| **Sound** | Shape<br>Distortion<br>Pitch<br>Glide<br>Interval<br>Noise<br>Resonance | 13<br>4<br>9<br>5<br>14<br>2<br>71 | | **Sampler** | Scrub<br>Grain Size<br>Play Speed<br>Timestretch<br>Scrub Jitter<br>Size Jitter<br>Speed Jitter<br>Step Offset | 15<br>16<br>17<br>18<br>116<br>117<br>118<br>85 | | **General** | Latch Toggle<br>Sample ID<br>Pattern ID<br>Synth Lvl<br>Synth Wet/Dry<br>Input Lvl<br>In Wet/Dry<br>HPF | 101<br>82<br>83<br>7<br>8<br>89<br>90<br>31 |
| **Env 1** | Sensitivity<br>Attack 1<br>Decay 1<br>Sustain 1<br>Release 1 | 3<br>73<br>75<br>74<br>72 | | **Delay** | Delay Send<br>Delay Time<br>Delay Ping Pong<br>Delay Wobble<br>Delay Feedback | 94<br>12<br>112<br>113<br>95 | | **Arp** | Arp Toggle<br>Arp Order<br>Arp Clock Div<br>Arp Chance<br>Arp Euclid Length<br>Arp Octaves | 102<br>103<br>104<br>105<br>106<br>107 |
| **Env 2** | Env 2 Level<br>Attack 2<br>Decay 2<br>Sustain 2<br>Release 2 | 19<br>20<br>21<br>22<br>23 | | **Reverb** | Reverb Send<br>Reverb Time<br>Reverb Shimmer<br>Reverb Wobble | 91<br>92<br>93<br>114 | | **Seq** | Seq Order<br>Seq Clock Div<br>Seq Chance<br>Seq Euclid Length<br>Gate Length | 108<br>109<br>110<br>111<br>11 |
| **LFO A** | LFO A Rate<br>LFO A Depth<br>LFO A Offset | 24<br>25<br>26 | | **LFO X** | LFO X Rate<br>LFO X Depth<br>LFO X Offset | 76<br>77<br>78 | | | | |
| **LFO B** | LFO B Rate<br>LFO B Depth<br>LFO B Offset | 27<br>28<br>29 | | **LFO Y** | LFO Y Rate<br>LFO Y Depth<br>LFO Y Offset | 79<br>80<br>81 | | | | |

---

## 3. Midi CC Table (Reverse Lookup)

|     | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|-----|---|---|---|---|---|---|---|---|
| **0** | - | *[Mod Wheel]* | Noise | Sensitivity | Distortion | Glide | *[Data MSB]* | Synth Lvl |
| **8** | Synth Wet/Dry | Pitch | - | Gate Length | Delay Time | Shape | Interval | Scrub |
| **16** | Grain Size | Play Speed | Timestretch | Env 2 Level | Attack 2 | Decay 2 | Sustain 2 | Release 2 |
| **24** | LFO A Rate | LFO A Depth | LFO A Offset | LFO B Rate | LFO B Depth | LFO B Offset | - | HPF |
| **32 - 63** | - | - | *[Reserved* | *for* | *CC14* | *LSB]* | - | - |
| **64** | *[Sustain]* | - | - | - | - | - | - | Resonance |
| **72** | Release 1 | Attack 1 | Sustain 1 | Decay 1 | LFO X Rate | LFO X Depth | LFO X Offset | LFO Y Rate |
| **80** | LFO Y Depth | LFO Y Offset | Sample ID | Pattern ID | - | Step Offset | - | - |
| **88** | - | Input Lvl | In Wet/Dry | Reverb Send | Reverb Time | Reverb Shimmer | Delay Send | Delay Feedback |
| **96** | *[Data Increment]* | *[Data Decrement]* | *[NRPN LSB]* | *[NRPN MSB]* | - | Latch Toggle | Arp Toggle | Arp Order |
| **104** | Arp Clock Div | Arp Chance | Arp Euclid Length | Arp Octaves | Seq Order | Seq Clock Div | Seq Chance | Seq Euclid Length |
| **112** | Delay Ping Pong | Delay Wobble | Reverb Wobble | - | Scrub Jitter | Size Jitter | Speed Jitter | - |

>Add row and column numbers to get the cc number

---

## 4. NRPN Protocol
NRPNs are always enabled. NRPNs can be used to set any of the Plinky parameters with 14 bit precision. Additionally, NRPNs can be used to set the values of the polyphonic parameters on a per-string basis.

### General Use
- Set the **string number** with CC 98 (NRPN MSB - use 0 for non-polyphonic commands)
- Set the **parameter number** with CC 99 (NRPN LSB)
- Set the 14 bit parameter **value** with CC 6 (Data MSB) and CC 38 (Data LSB)

### Rules
- It does not matter whether MSB or LSB is sent first
- It is allowed to update the string number without updating the parameter number and vice versa
- When either string or parameter number is changed, both value MSB and LSB need to be sent
- With no changes to string or parameter number, sending just a value MSB or LSB will update the parameter

### Examples
- When only working with non-polyphonic commands, string number just needs to be set to zero once with CC 98 in the entire session
- After succesfully updating a parameter by sending string number, parameter number and value MSB and LSB, small changes can be made to the parameter by sending CC 38 without the need for resending any of the other CCs

### Setting Polyphonic Parameters
>The polyphonic parameters are NRPN 0 - 22, 48 - 51 and 54 - 56
#### Global Channel
The global channel is either the midi in channel (when not in MPE mode) or the manager channel (when in MPE mode)

To set a polyphonic parameter through the global channel, set the desired string number with CC 98 before sending the parameter value
#### Member Channel
Member channels are the per-string midi channels when in MPE mode

To set a polyphonic parameter directly on a string, send the NPRN commands on its dedicated member midi channel. The string number must always be set to zero. If the string number on a member channel is set to anything other than zero, values will be ignored

>When trying to set a non-polyphonic parameter on a specific string, that parameter will be set globally

---

## 5. NRPN Table
| Section | | 0 | 1 | 2 | 3 | 4 | 5 |
|---------|-----|---|---|---|---|---|---|
| **Sound 1** | **0** | Shape | Distortion | Pitch | Octave | Glide | Interval |
| **Sound 2** | **6** | Noise | Resonance | Degree | Scale | Microtone | Column |
| **Envelope 1** | **12** | Sensitivity | Attack 1 | Decay 1 | Sustain 1 | Release 1 | - |
| **Envelope 2** | **18** | Env 2 Level | Attack 2 | Decay 2 | Sustain 2 | Release 2 | - |
| **Delay** | **24** | Delay Send | Delay Time | Delay Ping Pong | Delay Wobble | Delay Feedback | Tempo |
| **Reverb** | **30** | Reverb Send | Reverb Time | Reverb Shimmer | Reverb Wobble | - | Swing |
| **Arp** | **36** | Arp Toggle | Arp Order | Arp Clock Div | Arp Chance | Arp Euclid Length | Arp Octaves |
| **Sequencer** | **42** | Latch Toggle | Seq Order | Seq Clock Div | Seq Chance | Seq Euclid Length | Gate Length |
| **Sampler 1** | **48** | Scrub | Grain Size | Play Speed | Timestretch | Sample ID | Pattern ID |
| **Sampler 2** | **54** | Scrub Jitter | Size Jitter | Speed Jitter | - | - | Step Offset |
| **LFO A** | **60** | LFO A CV Depth | LFO A Offset | LFO A Depth | LFO A Rate | LFO A Shape | LFO A Symmetry |
| **LFO B** | **66** | LFO B CV Depth | LFO B Offset | LFO B Depth | LFO B Rate | LFO B Shape | LFO B Symmetry |
| **LFO X** | **72** | LFO X CV Depth | LFO X Offset | LFO X Depth | LFO X Rate | LFO X Shape | LFO X Symmetry |
| **LFO Y** | **78** | LFO Y CV Depth | LFO Y Offset | LFO Y Depth | LFO Y Rate | LFO Y Shape | LFO Y Symmetry |
| **Mixer 1** | **84** | Synth Lvl | Synth Wet/Dry | HPF | - | - | Volume |
| **Mixer 2** | **90** | Input Lvl | In Wet/Dry | - | - | - | Mix Width |
| | | | | | | | |

>Add row and column numbers to get the nrpn number
