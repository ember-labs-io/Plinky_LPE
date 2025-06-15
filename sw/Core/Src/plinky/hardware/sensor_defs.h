#pragma once
#include "utils.h"

#define READ_PHASES 13

// stm32 bitmasks

#define FF0 TSC_GROUP1_IO2 + TSC_GROUP4_IO2
#define FF1 TSC_GROUP2_IO2 + TSC_GROUP5_IO2
#define FF2 TSC_GROUP3_IO3 + TSC_GROUP6_IO2
#define FF3 TSC_GROUP1_IO3 + TSC_GROUP4_IO3
#define FF4 TSC_GROUP2_IO3 + TSC_GROUP5_IO3
#define FF5 TSC_GROUP3_IO4 + TSC_GROUP6_IO3
#define FF6 TSC_GROUP1_IO4 + TSC_GROUP4_IO4
#define FF7 TSC_GROUP2_IO4 + TSC_GROUP5_IO4
#define FF8a TSC_GROUP7_IO2
#define FF8b TSC_GROUP7_IO3

#define SS0 TSC_GROUP1_IO1 + TSC_GROUP4_IO1
#define SS1 TSC_GROUP2_IO1 + TSC_GROUP5_IO1
#define SS2 TSC_GROUP3_IO2 + TSC_GROUP6_IO1
#define SS3 TSC_GROUP1_IO1 + TSC_GROUP4_IO1
#define SS4 TSC_GROUP2_IO1 + TSC_GROUP5_IO1
#define SS5 TSC_GROUP3_IO2 + TSC_GROUP6_IO1
#define SS6 TSC_GROUP1_IO1 + TSC_GROUP4_IO1
#define SS7 TSC_GROUP2_IO1 + TSC_GROUP5_IO1
#define SS8a TSC_GROUP7_IO1
#define SS8b TSC_GROUP7_IO1

// config bitmasks for TSC, sets up the correct groups for reading
static u32 const channels_io[READ_PHASES] = {
    FF0 + FF1 + FF2 + FF8a, FF3 + FF4 + FF5 + FF8b, FF6 + FF7, FF0, FF1, FF2, FF3, FF4, FF5, FF6, FF7, FF8a, FF8b};
static u32 const sample_io[READ_PHASES] = {
    SS0 + SS1 + SS2 + SS8a, SS3 + SS4 + SS5 + SS8b, SS6 + SS7, SS0, SS1, SS2, SS3, SS4, SS5, SS6, SS7, SS8a, SS8b};

// clang-format off

// sensor ids belonging to the strings being read out in a phase
static u8 const reading_sensor[] = {
    // phase 0
    0, 2, 4, 1, 3, 5, 16,
    // phase 1
    6, 8, 10, 7, 9, 11, 17,
    // phase 2
    12, 14, 13, 15,
    // phase 3
    18, 19,
    // phase 4
    20, 21,
    // phase 5
    22, 23,
    // phase 6
    24, 25,
    // phase 7
    26, 27,
    // phase 8
    28, 29,
    // phase 9
    30, 31,
    // phase 10
    32, 33,
    // phase 11
    34,
    // phase 12
    35,
    // dummy to prevent out of bounds reading
    255
};

// groups belonging to the strings being read out in a phase
static const u8 reading_group[] = {
    // phase 0
    0, 1, 2, 3, 4, 5, 6,
    // phase 1
    0, 1, 2, 3, 4, 5, 6,
    // phase 2
    0, 1, 3, 4,
    // phase 3
    0, 3,
    // phase 4
    1, 4,
    // phase 5
    2, 5,
    // phase 6
    0, 3,
    // phase 7
    1, 4,
    // phase 8
    2, 5,
    // phase 9
    0, 3,
    // phase 10
    1, 4,
    // phase 11
    6,
    // phase 12
    6,
    // dummy to prevent out of bounds reading
    255
};

// clang-format on

static const u8 max_readings_in_phase[READ_PHASES] = {7, 14, 18, 20, 22, 24, 26, 28, 30, 32, 34, 35, 36};