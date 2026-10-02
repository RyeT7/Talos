#pragma once

#include "ir_config.h"

#include <stdbool.h>
#include <stdint.h>

#define SHARP_AC_STATE_LEN 13
#define SHARP_AC_BITS ( SHARP_AC_STATE_LEN * 8 )

#define SHARP_AC_MIN_TEMP 15
#define SHARP_AC_MAX_TEMP 30

#define SHARP_AC_CONSOLE_LINE_LEN 64

#define SHARP_AC_BYTE_TEMP 4
#define SHARP_AC_BYTE_POWER 5
#define SHARP_AC_BYTE_MODE 6
#define SHARP_AC_BYTE_SWING 8
#define SHARP_AC_BYTE_SPECIAL 10
#define SHARP_AC_BYTE_ION 11
#define SHARP_AC_BYTE_CHECKSUM 12

enum SharpAcMode {
    SHARP_AC_AUTO = 0b00,
    SHARP_AC_HEAT = 0b01,
    SHARP_AC_COOL = 0b10,
    SHARP_AC_DRY = 0b11,
};

enum SharpAcFan {
    SHARP_AC_FAN_AUTO = 0b010,
    SHARP_AC_FAN_MIN = 0b100,
    SHARP_AC_FAN_MED = 0b011,
    SHARP_AC_FAN_HIGH = 0b101,
    SHARP_AC_FAN_MAX = 0b111,
};

enum SharpAcSwing {
    SHARP_AC_SWING_IGNORE = 0b000,
    SHARP_AC_SWING_OFF = 0b010,
    SHARP_AC_SWING_TOGGLE = 0b111,
};

enum SharpAcPower {
    SHARP_AC_POWER_ON_FROM_OFF = 0x1,
    SHARP_AC_POWER_OFF = 0x2,
    SHARP_AC_POWER_ON = 0x3,
    SHARP_AC_POWER_SPECIAL_ON = 0x6,
    SHARP_AC_POWER_SPECIAL_OFF = 0x7,
};

enum SharpAcSpecial {
    SHARP_AC_SPECIAL_POWER = 0x00,
    SHARP_AC_SPECIAL_TURBO = 0x01,
    SHARP_AC_SPECIAL_ECONO = 0x04,
    SHARP_AC_SPECIAL_FAN = 0x05,
    SHARP_AC_SPECIAL_SWING = 0x06,
};

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

static const uint8_t SHARP_AC_DEFAULT_FRAME[SHARP_AC_STATE_LEN] = {
    0xAA, 0x5A, 0xCF, 0x10, 0x00, 0x01, 0x00, 0x00, 0x08, 0x80, 0x00, 0xE0, 0x01
};

static const struct IrTiming SHARP_AC_TIMING = {
    .hdr_mark = 3'800,
    .hdr_space = 1'900,
    .bit_mark = 470,
    .zero_space = 500,
    .one_space = 1'400,
    .gap = 20'000,
};

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

static uint8_t sharp_ac_frame[SHARP_AC_STATE_LEN];
