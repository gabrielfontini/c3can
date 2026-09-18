#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "driver/twai.h"


esp_err_t can_twai_set_bitrate(uint32_t bitrate);

esp_err_t can_twai_open(bool listen_only);

esp_err_t can_twai_close(void);

bool can_twai_is_open(void);

uint32_t can_twai_get_bitrate(void);


esp_err_t can_twai_send(
    const twai_message_t *message,
    TickType_t timeout
);


esp_err_t can_twai_receive(
    twai_message_t *message,
    TickType_t timeout
);

uint8_t can_twai_get_slcan_status(void);