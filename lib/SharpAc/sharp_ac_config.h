#pragma once

#include "ir_config.h"

#include <stdbool.h>
#include <stdint.h>

// a Sharp AC frame is 13 bytes so 104 bits
#define SHARP_AC_STATE_LEN 13
#define SHARP_AC_BITS ( SHARP_AC_STATE_LEN * 8 )

// temp range the AC accepts
#define SHARP_AC_MIN_TEMP 15
#define SHARP_AC_MAX_TEMP 30

// max length of one line typed in the serial console
#define SHARP_AC_CONSOLE_LINE_LEN 64

// which byte in the frame holds which setting
#define SHARP_AC_BYTE_TEMP 4
#define SHARP_AC_BYTE_POWER 5
#define SHARP_AC_BYTE_MODE 6
#define SHARP_AC_BYTE_SWING 8
#define SHARP_AC_BYTE_SPECIAL 10
#define SHARP_AC_BYTE_ION 11
#define SHARP_AC_BYTE_CHECKSUM 12

// AC modes, these go in the low 2 bits of the mode byte
enum SharpAcMode {
    SHARP_AC_AUTO = 0b00,
    SHARP_AC_HEAT = 0b01,
    SHARP_AC_COOL = 0b10,
    SHARP_AC_DRY = 0b11,
};

// fan speeds, the numbers look random but that's what the remote actually sends
enum SharpAcFan {
    SHARP_AC_FAN_AUTO = 0b010,
    SHARP_AC_FAN_MIN = 0b100,
    SHARP_AC_FAN_MED = 0b011,
    SHARP_AC_FAN_HIGH = 0b101,
    SHARP_AC_FAN_MAX = 0b111,
};

// swing values, ignore means leave it how it is
enum SharpAcSwing {
    SHARP_AC_SWING_IGNORE = 0b000,
    SHARP_AC_SWING_OFF = 0b010,
    SHARP_AC_SWING_TOGGLE = 0b111,
};

// power values, the AC wants a different one when turning on from off and when it's already on
enum SharpAcPower {
    SHARP_AC_POWER_ON_FROM_OFF = 0x1,
    SHARP_AC_POWER_OFF = 0x2,
    SHARP_AC_POWER_ON = 0x3,
    SHARP_AC_POWER_SPECIAL_ON = 0x6,
    SHARP_AC_POWER_SPECIAL_OFF = 0x7,
};

// tells the AC which button got pressed, basically
enum SharpAcSpecial {
    SHARP_AC_SPECIAL_POWER = 0x00,
    SHARP_AC_SPECIAL_TURBO = 0x01,
    SHARP_AC_SPECIAL_ECONO = 0x04,
    SHARP_AC_SPECIAL_FAN = 0x05,
    SHARP_AC_SPECIAL_SWING = 0x06,
};

// what we think the AC is doing right now, since the AC can't tell us
struct SharpAcState {
    bool power;
    enum SharpAcMode mode;
    uint8_t temp;
    enum SharpAcFan fan;
    bool swing;
    bool turbo;
    bool econo;
    bool ion;
};

// base frame we start from every time, then we change the bits we need
static const uint8_t SHARP_AC_DEFAULT_FRAME[SHARP_AC_STATE_LEN] = {
    0xAA, 0x5A, 0xCF, 0x10, 0x00, 0x01, 0x00, 0x00, 0x08, 0x80, 0x00, 0xE0, 0x01
};

// Sharp's IR timings in microseconds
static const struct IrTiming SHARP_AC_TIMING = {
    .hdr_mark = 3'800,
    .hdr_space = 1'900,
    .bit_mark = 470,
    .zero_space = 500,
    .one_space = 1'400,
    .gap = 20'000,
};

// starting state, off and cool at 24
static struct SharpAcState sharp_ac = {
    .power = false,
    .mode = SHARP_AC_COOL,
    .temp = 24,
    .fan = SHARP_AC_FAN_AUTO,
    .swing = false,
    .turbo = false,
    .econo = false,
    .ion = false,
};

// the frame we're about to send
static uint8_t sharp_ac_frame[SHARP_AC_STATE_LEN];
