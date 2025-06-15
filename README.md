# Plinky Firmware - Lucky Phoenix Edition

This is a user-maintained branch of firmware for the Plinky and Plinky+ synths. For the official firmware and information about the Plinky devices, visit the [official firmware repo](https://github.com/plinkysynth/plinky_public) and [official website](https://plinkysynth.com).

### About the refactor
This repo was branched from the [official firmware](https://github.com/plinkysynth/plinky_public) at v0.B0. During the refactor the code was updated to also include all updates in v0.B1 and v0.B2. At the end of the refactor (marked by the commit named "End of v0.B2 refactor") the firmware should behave almost completely identically to official firmware v0.B2. Two notable exceptions are the calibration procedure and the usb editor, which are currently absent and will be added later. Also some very minor changes may have snuck in here and there.

The main reason for doing a full refactor was to make future development easier and faster, both for myself and other people wanting to contribute. Work was done on:
- **Organization:** All code has been organized into modules and the codebase is now much easier to navigate
- **Consistency and descriptive naming:** The majority of the code now uses consistent casing and is named descriptively
- **Documentation:** The majority of the code has documentation interspersed with the code
- **Efficiency and safety:** Where possible, variables have been made local and many variables have been given appropriate data types (intN_t or uintN_t, as opposed to the generic int.) This didn't specifically have a high priority from a standpoint of the code not being efficient enough, as much as that it helps making the code easier to read, debug and extend.