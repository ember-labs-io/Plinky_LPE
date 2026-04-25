## NRPN implementation
### Usage
Plinky can send and receive 14bit values for all parameters and modulations. Receiving NRPNs is always enabled. Sending NRPNs can be enabled in the [Midi out Filter 1](settings_menu.md#filter-1) option of the Settings Menu. When sending NRPNs is enabled, [pushing parameter values](settings_menu.md#push-preset) over midi also uses NRPNs instead of CCs.

###### A default NRPN message consists of four CCs:
| | | |
|---|---|---|
| **CC 99 N** | NRPN MSB | N = [Page index](#nrpn-page-index) |
| **CC 98 M** | NRPN LSB | M = [Parameter index](#nrpn-parameter-index) |
| **CC 6 P** | Data Entry MSB | P = Value upper 7 bits |
| **CC 38 Q** | Data Entry LSB | Q = Value lower 7 bits |

###### Some of these can be omitted:
CC98/99 and CC6/38 form two 14bit values. When sending a 14bit value, each of the CCs in the pair need to be sent at least once. After having sent each at least once, the 14bit value can be updated by sending one of the CCs of the pair while omitting the other.

When the 14bit NRPN value changes, the 14bit DATA value is cleared. This means CC6/38 always need to both be sent at least once after a change of CC98/99.

CCs 96 (data increment) and 97 (data decrement) are supported.

---

#### NRPN Page ids
| MSB | Page |
|---|---|
| **0** | Set parameter value for all strings (same behavior as mono-timbral editing mode on Plinky) |
| **1** | Set Envelope 2 modulation to this parameter |
| **2** | Set Pressure modulation to this parameter |
| **3** | Set LFO A modulation to this parameter |
| **4** | Set LFO B modulation to this parameter |
| **5** | Set LFO X modulation to this parameter |
| **6** | Set LFO Y modulation to this parameter |
| **7** | Set Random modulation to this parameter |
| **8-15** | Set multi-timbral value of this parameter on string 1-8<br>If the parameter is not multi-timbral, the parameter will be set for all strings<br>(same behavior as multi-timbral editing mode on Plinky) |
| **16-23** | Request parameters from pages 0-7 |

###### NRPNs and MPE
On manager channels, all pages are recognized.  
On member channels, only page 0 is recognized. The behavior is the same as pages 8-15 on a manager channel

---

#### NRPN Parameter ids
| Section | | 0 | 1 | 2 | 3 | 4 | 5 |
|---------|-----|---|---|---|---|---|---|
| **Sound 1** | **0** | Shape | Distortion | Pitch | Octave | Glide | Interval |
| **Sound 2** | **6** | Noise | Resonance | Degree | Scale | Microtone | Column |
| **Envelope 1** | **12** | Sensitivity | Attack 1 | Decay 1 | Sustain 1 | Release 1 | Root |
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
| **Mixer 1** | **84** | Synth Level | Synth Wet/Dry | HPF | - | - | - |
| **Mixer 2** | **90** | Input Level | In Wet/Dry | - | - | - | Mix Width |
| | | | | | | | |

*Add row and column to get the parameter index*
