#include "board_config.h"

#ifndef ACTIVE_BOARD_HEADER
#error "ACTIVE_BOARD_HEADER not defined. Select a PlatformIO environment that sets it (see platformio.ini)."
#endif
#include ACTIVE_BOARD_HEADER

const BoardConfig& active_board() { return kActiveBoard; }
