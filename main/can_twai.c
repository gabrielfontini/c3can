#include "can_twai.h"

#include "driver/twai.h"
#include "driver/gpio.h"


#define CAN_TX_GPIO    GPIO_NUM_4
#define CAN_RX_GPIO    GPIO_NUM_5


static bool driver_installed = false;
static bool can_running = false;

static uint32_t selected_bitrate = 250000;

static uint32_t last_rx_missed_count = 0;
static uint32_t last_rx_overrun_count = 0;


/* ============================================================
 * BIT TIMING
 * ============================================================ */

static esp_err_t get_timing_config(
    uint32_t bitrate,
    twai_timing_config_t *timing
)
{
    if (timing == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }


    switch (bitrate)
    {
        case 10000:
        {
            twai_timing_config_t cfg =
                TWAI_TIMING_CONFIG_10KBITS();

            *timing = cfg;

            return ESP_OK;
        }


        case 20000:
        {
            twai_timing_config_t cfg =
                TWAI_TIMING_CONFIG_20KBITS();

            *timing = cfg;

            return ESP_OK;
        }


        case 50000:
        {
            twai_timing_config_t cfg =
                TWAI_TIMING_CONFIG_50KBITS();

            *timing = cfg;

            return ESP_OK;
        }


        case 100000:
        {
            twai_timing_config_t cfg =
                TWAI_TIMING_CONFIG_100KBITS();

            *timing = cfg;

            return ESP_OK;
        }


        case 125000:
        {
            twai_timing_config_t cfg =
                TWAI_TIMING_CONFIG_125KBITS();

            *timing = cfg;

            return ESP_OK;
        }


        case 250000:
        {
            twai_timing_config_t cfg =
                TWAI_TIMING_CONFIG_250KBITS();

            *timing = cfg;

            return ESP_OK;
        }


        case 500000:
        {
            twai_timing_config_t cfg =
                TWAI_TIMING_CONFIG_500KBITS();

            *timing = cfg;

            return ESP_OK;
        }


        case 800000:
        {
            twai_timing_config_t cfg =
                TWAI_TIMING_CONFIG_800KBITS();

            *timing = cfg;

            return ESP_OK;
        }


        case 1000000:
        {
            twai_timing_config_t cfg =
                TWAI_TIMING_CONFIG_1MBITS();

            *timing = cfg;

            return ESP_OK;
        }


        default:
        {
            return ESP_ERR_NOT_SUPPORTED;
        }
    }
}


/* ============================================================
 * CONFIGURA BITRATE
 * ============================================================ */

esp_err_t can_twai_set_bitrate(
    uint32_t bitrate
)
{
    /*
     * LAWICEL só permite alterar bitrate
     * com CAN fechado.
     */
    if (driver_installed || can_running)
    {
        return ESP_ERR_INVALID_STATE;
    }


    twai_timing_config_t test_config;


    esp_err_t result =
        get_timing_config(
            bitrate,
            &test_config
        );


    if (result != ESP_OK)
    {
        return result;
    }


    selected_bitrate = bitrate;


    return ESP_OK;
}


/* ============================================================
 * OPEN CAN
 * ============================================================ */

esp_err_t can_twai_open(
    bool listen_only
)
{
    if (driver_installed || can_running)
    {
        return ESP_ERR_INVALID_STATE;
    }


    twai_timing_config_t timing;


    esp_err_t result =
        get_timing_config(
            selected_bitrate,
            &timing
        );


    if (result != ESP_OK)
    {
        return result;
    }


    twai_mode_t mode =
        listen_only
        ? TWAI_MODE_LISTEN_ONLY
        : TWAI_MODE_NORMAL;


    twai_general_config_t general =
        TWAI_GENERAL_CONFIG_DEFAULT(
            CAN_TX_GPIO,
            CAN_RX_GPIO,
            mode
        );


    /*
     * Aumentamos as filas em relação
     * aos defaults.
     */
    general.tx_queue_len = 32;
    general.rx_queue_len = 64;


    twai_filter_config_t filter =
        TWAI_FILTER_CONFIG_ACCEPT_ALL();


    result =
        twai_driver_install(
            &general,
            &timing,
            &filter
        );


    if (result != ESP_OK)
    {
        return result;
    }


    driver_installed = true;


    result =
        twai_start();


    if (result != ESP_OK)
    {
        twai_driver_uninstall();

        driver_installed = false;

        return result;
    }


    can_running = true;

    last_rx_missed_count = 0;
    last_rx_overrun_count = 0;


    return ESP_OK;
}


/* ============================================================
 * CLOSE CAN
 * ============================================================ */

esp_err_t can_twai_close(void)
{
    if (!driver_installed)
    {
        can_running = false;

        return ESP_OK;
    }


    twai_status_info_t status;


    if (
        twai_get_status_info(
            &status
        ) == ESP_OK
    )
    {
        if (
            status.state ==
            TWAI_STATE_RUNNING
        )
        {
            esp_err_t result =
                twai_stop();


            if (result != ESP_OK)
            {
                return result;
            }
        }
    }


    esp_err_t result =
        twai_driver_uninstall();


    if (result != ESP_OK)
    {
        return result;
    }


    driver_installed = false;
    can_running = false;


    return ESP_OK;
}


/* ============================================================
 * STATUS
 * ============================================================ */

bool can_twai_is_open(void)
{
    return can_running;
}


uint32_t can_twai_get_bitrate(void)
{
    return selected_bitrate;
}

/* ============================================================
 * TRANSMIT
 * ============================================================ */

esp_err_t can_twai_send(
    const twai_message_t *message,
    TickType_t timeout
)
{
    if (!can_running)
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (message == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    return twai_transmit(
        message,
        timeout
    );
}


/* ============================================================
 * RECEIVE
 * ============================================================ */

esp_err_t can_twai_receive(
    twai_message_t *message,
    TickType_t timeout
)
{
    if (!can_running)
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (message == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    return twai_receive(
        message,
        timeout
    );
}

/* ============================================================
 * SLCAN STATUS FLAGS
 * ============================================================ */

uint8_t can_twai_get_slcan_status(void)
{
    if (!driver_installed)
    {
        return 0x00;
    }

    twai_status_info_t status;

    if (
        twai_get_status_info(&status)
        != ESP_OK
    )
    {
        return 0x00;
    }

    uint8_t flags = 0;


    /*
     * Bit 2 = Error Warning
     *
     * USBtin / LAWICEL:
     * EWARN
     *
     * ESP TWAI:
     * warning threshold = 96
     */
    if (
        status.tx_error_counter >= 96 ||
        status.rx_error_counter >= 96 ||
        status.state == TWAI_STATE_BUS_OFF
    )
    {
        flags |= 0x04;
    }


    /*
     * Bit 3 = Data Overrun
     *
     * Marcamos quando houve nova perda
     * desde a última leitura F.
     */
    if (
        status.rx_missed_count >
            last_rx_missed_count ||

        status.rx_overrun_count >
            last_rx_overrun_count
    )
    {
        flags |= 0x08;
    }


    /*
     * Depois da leitura, atualizamos a referência.
     *
     * Assim o bit de overrun funciona como
     * um evento "sticky até F", parecido com
     * o comportamento esperado do protocolo.
     */
    last_rx_missed_count =
        status.rx_missed_count;

    last_rx_overrun_count =
        status.rx_overrun_count;


    /*
     * Bit 5 = Error Passive
     */
    if (
        status.tx_error_counter >= 128 ||
        status.rx_error_counter >= 128 ||
        status.state == TWAI_STATE_BUS_OFF
    )
    {
        flags |= 0x20;
    }


    /*
     * Bit 7 = Bus Off
     *
     * No USBtin esse bit corresponde ao
     * TXBO do MCP2515.
     */
    if (
        status.state ==
        TWAI_STATE_BUS_OFF
    )
    {
        flags |= 0x80;
    }


    return flags;
}