#include "board_config.h"

#if defined(BOARD_PROFILE_ESP32DEV)
#include "boards/board_esp32dev.h"
#elif defined(BOARD_PROFILE_ESP32S3)
#include "boards/board_esp32s3.h"
#else
#error "No BOARD_PROFILE_* defined. Select a PlatformIO environment that sets one (see platformio.ini)."
#endif

const BoardConfig& active_board() {
#if defined(BOARD_PROFILE_ESP32DEV)
    return kBoardEsp32Dev;
#elif defined(BOARD_PROFILE_ESP32S3)
    return kBoardEsp32S3;
#endif
}
