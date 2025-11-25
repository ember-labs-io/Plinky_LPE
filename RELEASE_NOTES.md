# Release notes

[v0.4.0 - *The one with the manual saving*](#v040---the-one-with-the-manual-saving)  
[v0.3.1](#v031)  
[v0.3.0 - *The one with the parameters*](#v030---the-one-with-the-parameters)  
[v0.2.1](#v021)  
[v0.2.0 - *The one with the timing*](#v020---the-one-with-the-timing)  
[About the refactor (LPE v0.0.0)](#about-the-refactor-lpe-v000)

---

## v0.4.0 - *The one with the manual saving*
*Released on 26 November 2025*
### New Save/Load UI
- The currently active preset and pattern are no longer automatically saved
- Changes to the currently active preset and pattern do survive a reboot
- The load UI screen has been redesigned to make better use of the entire display
- In the load UI, one of the brightly lit pads (selected preset, pattern or sample) softly pulses. This indicates that this item will get cleared when long-pressing the Clear-pad. (Previously there was no way to see which item would be cleared)
- The load UI now works as follows:
    - Short press
        - Select this section to be cleared with the Clear pad
    - Long press (arrow at the right edge of the screen points up)
        - Load the pressed preset
        - Load the pressed pattern
        - Load/deactivate the pressed sample
    - Hold the Load-pad + long-press (arrow at the right edge of the screen points down)
        - Save current preset to selected slot
        - Save current pattern to selected slot
        - Open pressed sample in sample editor
### General UI
- Most long-press animations have been changed from fullscreen to a loading bar at the bottom of the screen
- All delay times for short/long presses have been centralized and are now identical across the system
- Renamed the strip with "shift pads" to "function pads" as only two of the eight pads are actually shifts
- Part of the noise filtering for presses has been centralized. All non-synth presses now have the same noise filtering strategy. (Previously function pads were filtered differently from parameter pads)
- Noise filtering on non-synth perssesis a little less strict, making them more responsive
- Precision-editing modulation amounts (shift + encoder turn) is now more intuitive
### Fixes
- Lfo rate matches properly when upgrading presets from OG to LPE firmware
- Latch properly clears when turned of by modulation
- Restored correct timing for synced delay time
- Restored exponential mapping of the sensitivity parameter to the depth of envelope 1
- Restored CV output for mod sources
- Restored modulation of voice parameters while the voice's envelope is in the release stage
- Shape only uses saw when its value is exactly 0.0
- Modulated shape value correctly stays within bounds of the same shape type
- Shape type is shown in the parameter name
- v0.3.0 removed the blocking deadzone at the center of the edit strip for bipolar parameters, this update implements a non-blocking deadzone in its place
- No leds light up when holding either of the Shift-pads when no parameter is selected
- Plinky blocks changing presets while it is receiving preset data from the web editor
- No synth note plays when opening the sample editor
- Display is cleared when rebooting from the settings menu 

---

## v0.3.1
*Released on 5 November 2025*
### Bugfixes:
- Corrects lfo rate mapping between OG and LPE firmware
- Corrects delay time of unsynced delay
- Restores CV A/B/X/Y input behavior
- Removes audio/led glitches on reboot

## v0.3.0 - *The one with the parameters*
*Released on 10 October 2025*
### Important
*This firmware affects the mapping of some parameters when it boots - it is recommended to backup your presets before installing!*
### Visual parameter cleanups
- Param names and values no longer overlap other ui elements
- Param values no longer jump font-sizes in 95%+ of the time
- Decimal values now use the same font size as the rest of the value
- All param values now show the appropriate amount of decimals
- All param sections and names are now named correctly
- All param sections and names now have appropriate icons
- Mod sources have an added ">>" in their name, to indicate modulation
- Positive values of offset-based parameters start with a "+" sign
- Synced time divisions are now expressed as intuitive fractions
- Unsynced time divisions are now expressed in (milli)seconds
- Params with different behavior on their positive/negative ranges (f.e. 8th vs 16th note swing) indicate those behaviors in their names, based on whether a positive/negative value is selected
- Unmodulated param values are now always on the top line
- If a param has any modulation assigned, the modulated value shows on the bottom line. (Including when the modulated value is equal to the unmodulated value)
- When a mod source is selected, the modulated value is still shown on the bottom line. This way the effect of the modulation is visible while editing its depth
    - There's an exception for params with long names (Scale, Arp Order, Seq Order, LFO Shape). These use two lines; they always show the modulated value and do not show when a mod source is selected
- Leds on the edit strip (leftmost column) are simplified and now much clearer
- Param value stays on screen shorter after being edited by the encoder
- When deselecting a param, the value will no longer stay on screen if it was recently edited with the encoder
- When selecting a parameter, its name will no longer flash on screen for a second before showing the param value
### Functional parameter cleanups
- All params now map to their full range
- All encoder skips have been removed - each turn of the encoder now affects the param value
- The volume parameter now has the same resolution and sensitivity as the other params
- The forced notch at zero, when editing bipolar params with the edit strip, has been removed. (The notches at +/-100% for Play Speed and Timestretch remain)
- Params without decimals (f.e. Octave, Arp Order) are mapped more evenly over the saved param range, making modulation more intuitive
- A number of parameters has been mapped in a more intuitive way:
    - Distortion, Wet/Dry (2x) and Mix Width are now unipolar
    - Delay 2nd Tap (PingPong) is now a negative percentage, reflecting actual behavior
    - Params that hold a synced and an unsynced time are fastest at the center, moving slower synced to the right, and slower unsynced to the left
- Euclidian functionality has been mapped in a more consistent way:
    - Positive chance values: when encountering an unplayed step, the progression **s**kips that step
    - Negative chance values: when encountering an unplayed step, the progression **w**aits on the current step
    - Chance behavior is reflected in an added (S) or (W) to the parameter name
    - Euclid Length is now unipolar
    - Implementation is identical for arp and sequencer
- Unused params can no longer be selected
### Parameter-related updates
- Arp and Latch are now real parameters, they can be modulated and edited with the encoder
- Arp, Seq and LFO synced timing now goes up to 32 bars
- Microtone is now scale-aware, at 100% you can slide smoothly between any two pads regardless of note-distance
- Holding shift now enables precision encoder editing - values move one step at a time
- When having an A param selected, pressing shift-B now switches to the B param instead of deselecting the A param (and vice versa)
- When having a mod source selected, pressing the shift pad now switches back to the base value instead of deselecting the param
- Pressing the encoder now toggles between the default value and your last chosen value (instead of the default value and zero)
- Clearing all modulation of a param now needs a longer encoder press
### System settings
- A settings menu was added to the Settings pad, currently holding the following settings:
    - Accelerometer sensitivity
    - Encoder direction
    - Midi in/out channel
    - CV quantization mode
- Press the encoder to toggle between choosing the setting and editing the value
- Additionally, the following actions can be executed from the settings menu:
    - Reboot
    - Touch calibration
    - CV calibration
    - Revert to OG presets
- Long-press the encoder to execute an action
### Hardware/Firmware compatibility
- This firmware can now run on the Plinky+
- To accommodate for new features and changed mapping of parameters, presets written by OG firmware will be automatically updated when Plinky boots
- A preset reverter is included in the settings menu. This will perfom the opposite process, making LPE presets playable on OG firmware with minimal sound differences

*Saved parameters have a limited resolution, rounding errors may occur*
### Bug fixes
- The preset load screen now always uses the correct index when initializing/copying
- A bug was fixed where the device would hang because of clashing calls to the usb driver
- A bug was fixed where the arp would not start immediately after a touch when using free timing
- A bug was fixed where pressure modulation could not reach its full depth

### Table of parameter changes (compared to OG firmware v0.B3)

| | | | | | | |
|---|---|---|---|---|---|---|
| **Sound 1** | **Shape** | **Distortion** | **Pitch** | **Octave** | **Glide** | **Interval** |
| | - | bi => unipolar | 2 decimals | no decimals | - | 2 decimals |
| **Sound 2** | **Noise** | **Resonance** | **Degree** | **Scale** | **Microtone** | **Column** |
| | - | - | no decimals | - | - | no decimals |
| **Env 1/2** | **Level** | **Attack** | **Decay** | **Sustain** | **Release** | [No Param] |
| | - | - | - | - | - | x |
| **Delay** | **Send** | **Time** | **Ping Pong** | **Wobble** | **Feedback** | **Tempo** |
| | - | free/synced flipped<br>fractions & durations | range: -100 to 0 | - | - | - |
| **Reverb** | **Send** | **Time** | **Shimmer** | **Wobble** | [No Param] | **Swing**  |
| | - | - | - | - | x | - |
| **Arp** | **Arp** | **Order** | **Clock Div** | **Chance** | **Euclid Len** | **Octaves** |
| | real param | - | unsynced inverted<br>fractions & durations<br>up to 32 bars | uni => bipolar | bi => unipolar<br>no decimals | no decimals |
| **Seq** | **Latch** | **Order** | **Clock Div** | **Chance** | **Euclid Len** | **Gate Len** |
| | real param | - | fractions & durations<br>up to 32 bars | - | bi => unipolar<br>no decimals | - |
| **Sample** | **Scrub** | **Grain Size** | **Play Speed** | **Timestretch** | **Sample** | **Pattern** |
| | - | - | - | - | no decimals | no decimals |
| **Sample** | **Scrub Jit** | **Size Jit** | **Speed Jit** | [No Param] | [No Param] | **Step Offset** |
| | - | - | - | x | x | no decimals |
| **LFO ABXY** | **CV Depth** | **Offset** | **Depth** | **Rate** | **Shape** | **Symmetry** |
| | - | - | - | synced divs added | - | - |
| **Mixer 1** | **Synth Lvl** | **Wet/Dry** | **High Pass** | [No Param] | **Settings** | **Volume** |
| | - | bi => unipolar | - | x | x | - |
| **Mixer 2** | **Input Lvl** | **In Wet/Dry** | [No Param] | [No Param]| **Settings** | **Mix Width** |
| | - | bi => unipolar | x | x | x | bi => unipolar |

---

## v0.2.1 
*Released on 3 August 2025*
### Re-implementation of existing v0.B2 features
- Restores touch and cv calibration procedures
- Restores usb bootloader mode
- Restores [web editor](https://plinkysynth.github.io/editor) functionality

*This makes this firmware a feature-complete replacement for official firmware v0.B2*

### Various small updates
- Adds support for midi sustain (CC 64)
- Some edge-case midi fixes

## v0.2.0 - *The one with the timing*
*Released on 14 July 2025*

### Important
*This version changes the parameter mapping for lfo rate - presets saved in earlier firmware versions may sound different because of this*

### Central clock
- Everything in the Plinky that is clock-synced, now syncs to one central clock
- The arpeggiator now always syncs to the sequencer correctly
- BPM limits of 30-240 bpm are now implemented consistently across the system

### External clock sources
- Syncing to an external clock source now has down-to-the-frame precision
- Plinky now correctly handles cv and midi clock coming in simultaneously
    - Plinky automatically follows the clock source with the highest priority, in the following order: cv, midi, internal
    - If an external clock source is absent for longer than one pulse at 30 bpm, Plinky drops to the clock source with the next highest priority. (euro clock listens at 4 ppqn, midi at 24 ppqn)
    - If the clock source changes while the sequencer is playing, Plinky tries to sync up the central clock to the new clock source with minimal disruption to the playing sequence. (results depend on the situation)
    - A message flashes on screen whenever the clock source changes

### Clock sync
- LFOs are now able to sync to the central clock
    - The lfo rate parameter works the same as the arp's Clock Div parameter: negative values set a free running speed, positive values set a sync value in 32nd notes
    - The frequencies of lfo shapes SmthRnd, StepRnd, BiTrigs and Trigs have been lowered to run at the same speed as the other lfo shapes and sync correctly with the clock
- Swing has been implemented system-wide and follows the swing parameter on the pad with the metronome icon
    - Synced arpeggiator, sequencer, lfos and the euro clock output all follow swung timing
    - Negative swing values set 16th note swing, positive swing values set 8th note swing
    - A swing value of 100 leads to a 3 : 1 ratio in swung notes length, a swing value of 66.7 leads to a 2 : 1 ratio (triplet swing)
    - Time inside swung notes is stretched or compressed, as opposed to only the swung notes themselves being offset. This means that synced elements with different durations than the swing duration still sync up accurately. (for example: a 16th note arpeggio still syncs up with an 8th note sequencer pattern when 8th note swing is active)
- Plinky now implements midi sync out
    - 24 ppqn clock is always sent
    - When the sequencer is started from the start step of the current sequence, Midi Start is sent
    - When the sequencer is started from any other step, Midi Continue is sent
    - When the sequener is stopped, Midi Stop is sent

### Various features
- The arp is now fully integrated into the "strings," which makes it so that:
    - The voices on screen now correctly show the notes played by the arp
    - The arp is now correctly sent over midi and to the eurorack output jacks
- The unsynced arpeggiator also implements swing. This simply scales the arpeggiator notes, as opposed to following the central clock. Positive and negative swing values give the same results
- Conditional steps (euclidian or true random) are now enabled for the sequencer when its Clock Div parameter is set to Gate CV

### Visuals
- New visuals have been created for step-recording
    - The visuals appear on the display as long as step-recording is active
    - The top half of the visuals represents the eight pressure-based substeps per sequencer step, the bottom half represents the four position-based substeps per step
    - When a pad-press starts, the substeps will start filling up left-to-right to represent the step being filled with touch data
    - When the step is full, the substeps will start moving right-to-left to represent new touch data being added at the end of the step and old data being pushed back
- Refined voice visualizations
    - The bars at the bottom of the screen now follow the actual envelopes of the voices, as opposed to the approximation it did before
    - The graphics themselves have been slightly refined
- The arp and latch flags now disappear when they have a chance of colliding with other graphics, increasing legibility
- The latch flag appears in inverted colors when it is disabled by step-recording being active. (the arp flag is always hidden during step-recording to make place for the new step-recording visuals)
- Small updates to the visuals for the tempo and swing parameters

### Fixes
- Step-recording now gives more intuitive and predictable results
- The arpeggiator is automatically disabled when step-recording is active, no longer interfering with the recording process
- Jumping to the first step of the sequencer now only happens on a short press of the Left Pad (as opposed to any press of the Left Pad) and only when pressed in the default ui (as opposed to also being able to be triggered from the "set start step" ui.) It now no longer interferes with using the Left Pad to smoothly jump between sub-sequences in a pattern

### Notes
The combination of the new features leads to some very interesting possibilities:
- Plinky can now be used as a midi to euro, and euro to midi clock converter
- Plinky can now be used as a midi to euro, and euro to midi transport converter. The first euro clock will start the sequencer (introduced in v0.B2) and will now also generate a Midi Start or Continue. Vice versa an incoming Midi Start or Continue will start playing the sequencer, which will start outputting euro clock
- When using the expander or Plinky+, the lfos can be used as clock dividers/multipliers in a eurorack setting by configuring them as synced square or trig waves. If necessary, the symmetry parameter can be used to turn a square wave into a short pulse
- Since the synced lfo timing is linked to the central clock, and certain actions reset the central clock (starting the sequencer, initiating a new arp), synced lfos are predictable and reproducable

---

### About the refactor (LPE v0.0.0)
This repo was branched from the [official firmware](https://github.com/plinkysynth/plinky_public) at v0.B0. During the refactor the code was updated to also include all updates in v0.B1 and v0.B2. At the end of the refactor (marked by the commit named "End of v0.B2 refactor") the firmware should behave almost completely identically to official firmware v0.B2. Two notable exceptions are the calibration procedure and the usb editor, which are currently absent and will be added later. Also some very minor changes may have snuck in here and there

The main reason for doing a full refactor was to make future development easier and faster, both for myself and other people wanting to contribute. Work was done on:
- **Organization:** All code has been organized into modules and the codebase is now much easier to navigate
- **Consistency and descriptive naming:** The majority of the code now uses consistent casing and is named descriptively
- **Documentation:** The majority of the code has documentation interspersed with the code
- **Efficiency and safety:** Where possible, variables have been made local and many variables have been given appropriate data types (intN_t or uintN_t, as opposed to the generic int.) This didn't specifically have a high priority from a standpoint of the code not being efficient enough, as much as that it helps making the code easier to read, debug and extend