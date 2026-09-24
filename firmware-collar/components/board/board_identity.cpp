#include "board_identity.h"

#include "cownect_config.h"

uint64_t board_device_id()
{
    return cownect::config::DEVELOPMENT_DEVICE_ID;
}

bool board_device_id_is_development()
{
    return true;  // no production provisioning scheme exists yet
}

const char* board_firmware_version()
{
    return cownect::config::FIRMWARE_VERSION;
}
