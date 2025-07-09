# Plinky Firmware - Lucky Phoenix Edition

This is a user-maintained branch of firmware for the Plinky and Plinky+ synths. For the official firmware and information about the Plinky devices, visit the [official firmware repo](https://github.com/plinkysynth/plinky_public) and [official website](https://plinkysynth.com).

## Current Release: v0.2.0 - *The one with the timing*
**Release date:** 15 July 2025

**Release state:** Alpha
- Some features are not implemented (calibration, usb editor)
- Presets saved in previous firmware versions may sound different
- Encountering bugs that were not present in official firmware v0.B2 is likely

**Binary:**

**How to install:**
- Download the binary above
- Follow the firmware installation instructions on [the official website](https://plinkysynth.com/firmware)
- If you're updating from an older firmware version, it might be necessary to rename the file to CURRENT.UF2 before installing
---
## Release notes

### v0.2.0 - *The one with the timing*

#### Important
- This version changes the parameter mapping for lfo rate - patches saved in earlier firmware versions may sound different because of this

#### Central clock
- Everything in the Plinky that is clock-synced, now syncs to one central clock
- The arpeggiator now always syncs to the sequencer correctly
- BPM limits of 30-240 bpm are now implemented consistently across the system

#### External clock sources
- Syncing to an external clock source now has down-to-the-frame precision
- Plinky now correctly handles cv and midi clock coming in simultaneously
    - Plinky automatically follows the clock source with the highest priority, in the following order: cv, midi, internal
    - If an external clock source is absent for longer than one pulse at 30 bpm, Plinky drops to the clock source with the next highest priority. (euro clock listens at 4 ppqn, midi at 24 ppqn)
    - If the clock source changes while the sequencer is playing, Plinky tries to sync up the central clock to the new clock source with minimal disruption to the playing sequence. (results depend on the situation)
    - A message flashes on screen whenever the clock source changes

#### Clock sync
- LFOs are now able to sync to the central clock
    - The lfo rate parameter works the same as the arp's clock div parameter: negative values set a free running speed, positive values set a sync value in 32nd notes
    - The frequencies of lfo shapes SmthRnd, StepRnd, BiTrigs and Trigs have been lowered to trigger at the same speed as the other lfo shapes and sync correctly with the clock
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

#### Various features
- The arp is now fully integrated into the "strings," which makes it so that:
    - The voices on screen now correctly show the notes played by the arp
    - The arp is now correctly sent to the eurorack output jacks
- The unsynced arpeggiator also implements swing. This simply scales the arpeggiator notes, as opposed to following the central clock. Positive and negative swing values give the same results
- Conditional steps (euclidian or true random) are now enabled for the sequencer when its clock div parameter is set to Gate CV

#### Fixes
- Step-recording now gives more intuitive and predictable results
- The arpeggiator is automatically disabled when step-recording is active, no longer interfering with the recording process
- Jumping to the first step of the sequencer now only happens on a short press of the Left Pad (as opposed to any press of the Left Pad) and only when pressed in the default ui (as opposed to also being able to be triggered from the "set start step" ui.) It now no longer interferes with using the Left Pad to smoothly jump between sub-sequences in a pattern.

#### Visuals
- New visuals are created for step-recording
- Refined voice visualizations
    - The bars on the bottom of the screen now follow the actual envelopes of the voices, as opposed to the approximation it did before
    - The graphics themselves have been slightly refined
- The arp and latch flags now disappear when they have a chance of colliding with other graphics, increasing legibility
- The latch flag appears in inverted colors when it is disabled by step-recording being active. (the arp flag is always hidden during step-recording to make place for the new step-recording visuals)
- Small updates to the visuals for the tempo and swing parameters

#### Notes
The combination of the new features leads to some very interesting possibilities:
- Plinky can now be used as a midi -> euro, and euro -> midi clock converter
- Plinky can now be used as a midi -> euro, and euro -> transport converter. The first euro clock will start the sequencer (introduced in v0.B2) and will now also generate a Midi Start or Continue. Vice versa an incoming Midi Start or Continue will start playing the sequencer, which will start outputting euro clock
- When using the expander or Plinky+, the lfos can be used as clock dividers/multipliers in a eurorack setting by configuring them as synced square or trig waves. If necessary, the symmetry parameter can be used to turn a square wave into a short pulse
- Since the synced lfo timing is linked to the central clock, and certain actions (starting the sequencer, initiating a new arp) reset the central clock, synced lfos are predictable and reproducable
---
### About the refactor
This repo was branched from the [official firmware](https://github.com/plinkysynth/plinky_public) at v0.B0. During the refactor the code was updated to also include all updates in v0.B1 and v0.B2. At the end of the refactor (marked by the commit named "End of v0.B2 refactor") the firmware should behave almost completely identically to official firmware v0.B2. Two notable exceptions are the calibration procedure and the usb editor, which are currently absent and will be added later. Also some very minor changes may have snuck in here and there.

The main reason for doing a full refactor was to make future development easier and faster, both for myself and other people wanting to contribute. Work was done on:
- **Organization:** All code has been organized into modules and the codebase is now much easier to navigate
- **Consistency and descriptive naming:** The majority of the code now uses consistent casing and is named descriptively
- **Documentation:** The majority of the code has documentation interspersed with the code
- **Efficiency and safety:** Where possible, variables have been made local and many variables have been given appropriate data types (intN_t or uintN_t, as opposed to the generic int.) This didn't specifically have a high priority from a standpoint of the code not being efficient enough, as much as that it helps making the code easier to read, debug and extend.