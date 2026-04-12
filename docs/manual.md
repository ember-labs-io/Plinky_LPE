# Plinky Manual

This is plinky, an 8 voice polyphonic touch synthesiser that specialises in fragile, melancholic sounds.
It supports 4 external CV modulation sources, called A B X Y, each with its own additional LFO on top.
A & B also have dedicated offset knobs.
Think of plinky as 8 vertical monophonic strings, played by touching the 64 main pads.
By default each string quantizes to steps of a C major scale, and the strings are a 5th apart.

Each string has:

- EITHER 4 sawtooth oscillators, detuned by the tiny movements of your finger
- OR 4 sample grains drawn from 1 of 8 samples that you can record into plinky.
- A white noise oscillator
- An ADSR envelope controlling...
- A resonant 2-pole low-pass gate
- A secondary Attack-Decay envelope with repeat.

Plinky also has global delay, reverb, high pass filter and saturation units along with a simple mixer, arpeggiator and sequencer.

You can play plinky straight away by touching the 64 main pads. The parameters and presets can be accessed using the row of 8 'shift' keys (with blue LEDs) along the bottom, which are then used in conjunction with the main pads to select parameters, presets, sequencer steps, and samples.

## Controls

Working from the bottom of the panel with the row of shift keys:

### Parameters 1/2

The most important buttons! (Prototype labels this A/B which is confusing — should be 1/2.)

Tap to enter parameter mode (LED on), then tap one of the main pads to choose a parameter to edit.
You can edit the parameter either with the left most column, which acts as a slider, or by sliding your finger up/down from the icon pad itself. TIP: you can hold the shift key with one hand, and edit the parameter with the other.

Tap 1/2 again to enter slider mode (LED flashing); the left most column remains a single large slider for your chosen parameter, but the rest of the main pads revert to being playable notes.

Tap 1/2 again to return to play mode (LED off).

### Preset Mode

LED on. The left 32 main pads become preset selectors. The middle 24 pads are pattern selectors. The rightmost column of 8 pads select/de-select a sample.

If the sequencer is playing, the change will happen on next loop.
If you press and hold a preset/pattern pad, the current preset/pattern will be copied over the one you hold.
If you press and hold a sample pad, you go into sample edit/record mode.
Press and hold the X shift button to initialize a preset or clear a pattern.

### Previous

Tap to move back a step (not playing) or reset the sequence (playing). If held down, the current loop is shown on the main pads. Tap a pad inside the current loop to jump to that step, or tap a pad outside the current loop to set a new loop start, that takes effect on next loop.

### Next

Tap to move forward a step. If held down, the current loop is shown on the main pads. Tap a pad to set the end of the current loop.

### Clear

Clears stuff! eg press to cancel a latched chord, to suppress notes 'live' from a playing sequence, to wipe a preset or pattern (in preset mode), or to clear note data (in record mode), or enter a rest (in step record mode).

### Record

Toggle record mode on or off. This combines with the play button. When time is running, recording is in realtime. When time is paused, this enables step sequencing.

### Play

Toggle time playing.

## Parameters

Parameters are grouped in rows, from bottom to top. There are two parameters per pad, depending on whether you pressed the Parameter '1' or '2' shift button. Like a primary and secondary function.

### Sampler Row

![seq]**Sample Choice** — selects which sample is active, or 0 for none. Allows automation of the current sample.  
![wave]**Noise Level** — the amount of white noise mixed with the main synth oscillator/sample player.

![right]**Scrub** — Adds an offset to the position within the sample whence the grains are plucked.  
![right]**Scrub jitter** — Randomises the scrub position per grain.

![right]**Rate** — changes the playback rate of the sample, slowing and pitching it like a record. Try negative for reverse.  
![right]**Rate jitter** — Randomises the rate per grain.

![period]**Grain Size** — changes the size of each grain, from metallic tiny grains to smooth long grains.  
![period]**Grain Size Jitter** — randomises the grain size per grain.

![time]**Timestretch** — changes the playback rate of the sample, without changing the pitch. Try negative for reverse.  
*(secondary — UNUSED)*

![phones]**Headphone Volume** — changes the master volume of the headphone jack on the underside of plinky.  
![cv-quantize]**Quantise mode** — selects how much quantisation is applied to the pitch CV input: from none, to chromatic, to rotations of the selected scale.

### Arp/Sequencer Row

![order]**Order** — changes the order of notes in the arpeggio.  
![order]**Order** — same for Sequencer.

![tempo]**Divide** — clock divider for the arpeggio.  
![tempo]**Divide** — same for Sequencer.

![percent]**Probability** — sets the chance of a note triggering in the arp. Negative values skip, positive values pause. Note that the rhythm is either euclidean or random depending on the next parameter.  
![percent]**Probability** — same for Sequencer.

![length]**Euclid** — How many steps in the euclidean rhythm set by the probability. 1 or 0 steps mean 'random'.  
![length]**Euclid** — same for sequencer.

![octave]**Arp Octave** — how many octaves to spread the Arp over.  
![seq]**Seq Pattern** — current pattern; allows CV / LFO control over sequencer pattern choice.

![play]**Tempo** — tap repeatedly to set a tap tempo, or use as a slider to set BPM. If 1/16th note clock pulses are received on the clock CV input, this is updated automatically.  
![interval]**Seq Step** — current step; allows CV / LFO control over sequencer step playback.

### VCA/Mixer Row

![touch]**Sensitivity** — how much pressure is needed to fully open the low pass gate. Low values give softer sine sounds.  
![jack]**Input Level** — level of the audio input jack, that is sent to output.

![distort]**Drive** — output drive level of the synth, before fx. High levels cause extreme guitar-like screams with polyphonic chords, due to intermodulation distortion.  
![wave]**Synth Level** — level of the dry synth audio in the output. Can be used to compensate for high drive levels being loud.

![adsr_a]**Attack** — attack time of the envelope that drives the low pass gate.  
![jack]**In→FX level** — wet level for the input audio.

![adsr_d]**Decay** — decay time of the envelope that drives the low pass gate.  
![reverb]**Wet** — wet/dry level for the fx. Bipolar.

![adsr_s]**Sustain** — sustain level of the envelope that drives the low pass gate.  
![hpf]**High Pass Cutoff** — cutoff frequency for the global high pass filter, good for taming any boominess.

![adsr_r]**Release** — release time of the envelope that drives the low pass gate.  
![distort]**Resonance** — unruly resonance control for the low pass gates.

### Pitch Row

![octave]**Octave** — base octave for the synth.  
![offset]**Rotate** — shifts all notes, but keeping them within the current scale.

![piano]**Pitch** — base pitch for the synth. Unquantized.  
![piano]**Scale** — selects which scale is used by the pads & rotate feature.

![glide]**Glide** — glide time. Each string glides independently.  
![micro]**Microtone** — amount of quantizing to apply. 0=fully quantized, 100=completely 'analog' pitches.

![offset]**Interval** — adds a fixed interval between the oscillators on each string. Try ±7 (a fifth) or ±12 (octave).  
![offset]**Stride** — sets the interval between successive strings from left to right.

![interval]**Gate length** — turn down from 100% to get more staccato notes from the Arpeggiator and Sequencer.  
![shape]**PWM** — at 0%, plinky produces 4 sawtooths per string. Above 0%, it flips & adjusts the phase of 2 of them, giving a pulse/square waveform whose width this controls.

![tempo]**Env2 Rate** — controls the speed of the 2nd envelope.  
![warp]**Env2 Warp** — controls whether the Envelope is a ramp down (-100), a triangle (0), or a ramp up (100).

### FX Row

![send]**Delay Send** — amount of dry signal to send to delay unit.  
![send]**Reverb Send** — same for reverb.

![tempo]**Delay Time** — length of the delay time. Negative values are beat synced, positive values are free.  
![time]**Reverb Time** — length of the reverb.

![tilt]**Second Tap** — values less than 100% cause the left and right stereo channels to have different delay lengths, giving a ping-pong effect. Try musical ratios like 50% or 75%.  
![feedback]**Shimmer** — level of reverb shimmer effect.

![wave]**Delay Wobble** — adds a wow/flutter LFO to the delay time, causing pitch shifts.  
![wave]**Reverb Wobble** — same for reverb.

![feedback]**Feedback** — amount of feedback in the delay.  
![cross]**Reverb** — unused.

![amplitude]**Env2 Level** — amount (depth) of the second envelope.  
![play]**Env2 Repeat** — repeat amount. At 100%, the envelope repeats forever; \<100%, it decays. 0% — it doesn't repeat.

### CV / LFO Rows

Each of the 4 CV modulation inputs is scaled and offset, then added to a dedicated LFO.

![amplitude]**Scale A** — attenuverter for the CV input A.  
![amplitude]**Scale B** — same for B.

![offset]**Offset A** — shift for the CV input A. Dedicated Knob A is always added to this.  
![offset]**Offset B** — same for B.

![amplitude]**Depth A** — LFO depth for A.  
![amplitude]**Depth B** — same for B.

![tempo]**Rate A** — LFO frequency for A.  
![tempo]**Rate B** — same for B.

![shape]**Shape A** — LFO shape for A.  
![shape]**Shape B** — same for B.

![warp]**Warp A** — changes the symmetry of the shape, eg ramp down → triangle → ramp up.  
![warp]**Warp B** — same for B.

### Toggle / Preview Row

![play]**Arp** — arp mode on/off.

The center 4 pads are always available to preview your patch.

![play]**Latch** — 'latch' previously held notes down.

## Modulation Sources

Right column. Every parameter for every string has its own sample & hold unit, and can also be modulated from a matrix of 7 sources.

When selecting a parameter to edit (Shift button 1 or 2 held / LED lit), you can also select a modulation source from this rightmost column. From top to bottom:

| Source     | Description                                                                                                               |
| ---------- | ------------------------------------------------------------------------------------------------------------------------- |
| Base Value | The value of the parameter before modulation.                                                                             |
| Random     | Amount of variation to add on each new trigger. Positive values are unipolar, negative values add a bipolar random value. |
| Pressure   | Finger pressure → parameter.                                                                                              |
| Env2       | Envelope 2 → parameter. TIP: try turning this up for the 'Noise' parameter (bottom left pad, Param page 2).               |
| A          | Input CV A + Knob A + LFO A → parameter.                                                                                  |
| B          | Input CV B + Knob B + LFO B → parameter.                                                                                  |
| X          | Input CV X + LFO X → parameter.                                                                                           |
| Y          | Input CV Y + LFO Y → parameter.                                                                                           |

> TIP: The modulation source you are editing is remembered. Don't forget to 'go back' to the 'base value' (top button) after editing modulations for the other channels.

## Sample Edit / Recording Mode

Plinky lets you record and use 8 samples, selected by the 8 sample pads. If you go into preset mode and long-press one of the 8 sample pads (rightmost column), you enter sample edit mode. Each sample is split into 8 'slices', corresponding to the 8 columns (strings) of plinky.

To record a sample, press and hold the record button. Plinky enters a 'set recording level' screen; you can use Knob A to adjust the peak level to neatly fill the screen. There is an additional +6dB headroom beyond this, but very loud inputs may still show 'CLIP!' warning.

Once you are happy, press the Play or Record button to 'arm'. As soon as a sound is heard, or on a second tap of play/record, plinky will start recording. To stop, press play/record again.

To slice into up to 8 pieces while recording, tap any of the main pad buttons. If you do not tap, plinky will cut the sample into 8 equal length slices, and you can edit the splits later.

Once you have recorded, to audition the slices, press and hold the main pad buttons. If you slide your finger up and down, it adjusts the start point of each slice.

There are two options associated with samples: Tape/Pitch mode and Loop mode. Press the 'Param 1' button (bottom left) to toggle between Tape and Pitch; press 'Param 2' button to cycle loop mode between 'play slice', 'loop slice', 'play all', 'loop all'.

**Tape mode** lays out the sample slices across all 64 pads, allowing you to quickly play from any point within the sample. The pitch of playback is only affected by the 'sample rate' parameter.

**Pitched mode** — each slice is assigned a base pitch, visible like 'C#3' in the display. You can set this reference pitch for each slice by sliding your finger in the lower half of the main pad area. Now when you perform notes in plinky, it will set up a multisample key-split and choose the closest pitched sample to the desired note. Be careful of octaves to be sure to use all your slices. If multiple slices have the same pitch, they will be round-robined.

## CV Inputs

Top to bottom:

| Jack       | Description                                                                                |
| ---------- | ------------------------------------------------------------------------------------------ |
| A, B, X, Y | Modulation inputs, each combined with its own LFO. A & B are also combined with the knobs. |
| Pitch      | Offsets the pitch of the entire synth via 1 V/Oct input. Approx -1v to 5v.                 |
| Gate       | Modulates the low pass gates of the entire synth. 0–5v.                                    |
| Clock In   | 1/16th note clock input, that drives the sequencer and arpeggiator.                        |
| Audio In   | A signal sent through to the output mixer via FX, also used in sampling mode.              |

## CV Outputs

Top to bottom:

| Jack     | Description                                                            |
| -------- | ---------------------------------------------------------------------- |
| Clock    | 1/16th note clock out.                                                 |
| Trigger  | +6v pulse on every new note.                                           |
| Pitch Hi | 1v/oct pitch of the rightmost string currently playing (respects Arp). |
| Gate     | Analog envelope follower of the loudest string currently playing.      |
| Pitch Lo | 1v/oct pitch of the leftmost string held down (ignores Arp).           |
| Pressure | Analog level of heaviest finger press.                                 |
| Right    | Right stereo audio out.                                                |
| Left     | Left stereo audio out (normalled to mono if no right jack plugged in). |
