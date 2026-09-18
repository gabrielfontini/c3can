#include "usb_slcan.h"
#include "can_twai.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "driver/usb_serial_jtag.h"

#include "esp_err.h"
#include "esp_timer.h"


#define USB_RX_BUFFER_SIZE     1024
#define USB_TX_BUFFER_SIZE     1024

#define SLCAN_COMMAND_SIZE     64


static char command_buffer[SLCAN_COMMAND_SIZE];

static size_t command_position = 0;

static bool timestamp_enabled = false;


static SemaphoreHandle_t usb_tx_mutex = NULL;


/* ============================================================
 * USB
 * ============================================================ */

static void usb_write_data(
    const void *data,
    size_t length
)
{
    if (
        data == NULL ||
        length == 0
    )
    {
        return;
    }


    if (usb_tx_mutex != NULL)
    {
        xSemaphoreTake(
            usb_tx_mutex,
            portMAX_DELAY
        );
    }


    int written =
        usb_serial_jtag_write_bytes(
            data,
            length,
            pdMS_TO_TICKS(1000)
        );


    if (written > 0)
    {
        usb_serial_jtag_wait_tx_done(
            pdMS_TO_TICKS(1000)
        );
    }


    if (usb_tx_mutex != NULL)
    {
        xSemaphoreGive(
            usb_tx_mutex
        );
    }
}


static void usb_write_string(
    const char *text
)
{
    if (text == NULL)
    {
        return;
    }


    usb_write_data(
        text,
        strlen(text)
    );
}


/* ============================================================
 * RESPOSTAS SLCAN
 * ============================================================ */

static void slcan_ok(void)
{
    const char response = '\r';

    usb_write_data(
        &response,
        1
    );
}


static void slcan_error(void)
{
    const char response = 0x07;

    usb_write_data(
        &response,
        1
    );
}


/* ============================================================
 * HEX
 * ============================================================ */

static int hex_to_int(
    char c
)
{
    if (
        c >= '0' &&
        c <= '9'
    )
    {
        return c - '0';
    }


    if (
        c >= 'A' &&
        c <= 'F'
    )
    {
        return c - 'A' + 10;
    }


    if (
        c >= 'a' &&
        c <= 'f'
    )
    {
        return c - 'a' + 10;
    }


    return -1;
}


static bool parse_hex(
    const char *text,
    size_t digits,
    uint32_t *value
)
{
    if (
        text == NULL ||
        value == NULL
    )
    {
        return false;
    }


    uint32_t result = 0;


    for (
        size_t i = 0;
        i < digits;
        i++
    )
    {
        int nibble =
            hex_to_int(
                text[i]
            );


        if (nibble < 0)
        {
            return false;
        }


        result =
            (result << 4) |
            (uint32_t)nibble;
    }


    *value = result;


    return true;
}


static char int_to_hex(
    uint8_t value
)
{
    static const char hex[] =
        "0123456789ABCDEF";


    return hex[
        value & 0x0F
    ];
}


/* ============================================================
 * TRANSMISSÃO CAN
 * ============================================================ */

static void process_can_transmit(
    const char *command
)
{
    if (
        command == NULL ||
        !can_twai_is_open()
    )
    {
        slcan_error();

        return;
    }


    char type =
        command[0];


    bool extended =
        (
            type == 'T' ||
            type == 'R'
        );


    bool rtr =
        (
            type == 'r' ||
            type == 'R'
        );


    if (
        type != 't' &&
        type != 'T' &&
        type != 'r' &&
        type != 'R'
    )
    {
        slcan_error();

        return;
    }


    size_t id_digits =
        extended ? 8 : 3;


    size_t minimum_length =
        1 +
        id_digits +
        1;


    size_t command_length =
        strlen(command);


    if (
        command_length <
        minimum_length
    )
    {
        slcan_error();

        return;
    }


    uint32_t identifier = 0;


    if (
        !parse_hex(
            &command[1],
            id_digits,
            &identifier
        )
    )
    {
        slcan_error();

        return;
    }


    /*
     * Validação do identificador
     */
    if (!extended)
    {
        if (
            identifier > 0x7FF
        )
        {
            slcan_error();

            return;
        }
    }
    else
    {
        if (
            identifier >
            0x1FFFFFFF
        )
        {
            slcan_error();

            return;
        }
    }


    int dlc =
        hex_to_int(
            command[
                1 +
                id_digits
            ]
        );


    if (
        dlc < 0 ||
        dlc > 8
    )
    {
        slcan_error();

        return;
    }


    /*
     * RTR não possui bytes de dados.
     */
    if (rtr)
    {
        if (
            command_length !=
            minimum_length
        )
        {
            slcan_error();

            return;
        }
    }
    else
    {
        size_t expected_length =
            minimum_length +
            ((size_t)dlc * 2);


        if (
            command_length !=
            expected_length
        )
        {
            slcan_error();

            return;
        }
    }


    twai_message_t message = {0};


    message.identifier =
        identifier;

    message.data_length_code =
        dlc;

    message.extd =
        extended;

    message.rtr =
        rtr;


    /*
     * Converte dados ASCII HEX
     * em bytes.
     */
    if (!rtr)
    {
        size_t data_position =
            minimum_length;


        for (
            int i = 0;
            i < dlc;
            i++
        )
        {
            uint32_t byte_value = 0;


            if (
                !parse_hex(
                    &command[
                        data_position +
                        (i * 2)
                    ],
                    2,
                    &byte_value
                )
            )
            {
                slcan_error();

                return;
            }


            message.data[i] =
                (uint8_t)byte_value;
        }
    }


    esp_err_t result =
        can_twai_send(
            &message,
            pdMS_TO_TICKS(100)
        );


    if (result != ESP_OK)
    {
        slcan_error();

        return;
    }


    /*
     * USBtin responde:
     *
     * z = frame padrão aceito
     * Z = frame extended aceito
     */
    if (extended)
    {
        usb_write_string(
            "Z\r"
        );
    }
    else
    {
        usb_write_string(
            "z\r"
        );
    }
}


/* ============================================================
 * CONVERTE CAN RECEBIDO → SLCAN
 * ============================================================ */

static void send_can_frame_to_usb(
    const twai_message_t *message
)
{
    if (message == NULL)
    {
        return;
    }


    char output[40];

    size_t position = 0;


    /*
     * Tipo
     */
    if (message->rtr)
    {
        output[position++] =
            message->extd
            ? 'R'
            : 'r';
    }
    else
    {
        output[position++] =
            message->extd
            ? 'T'
            : 't';
    }


    /*
     * ID
     */
    int id_digits =
        message->extd
        ? 8
        : 3;


    for (
        int shift =
            (id_digits - 1) * 4;

        shift >= 0;

        shift -= 4
    )
    {
        output[position++] =
            int_to_hex(
                (
                    message->identifier
                    >> shift
                ) & 0x0F
            );
    }


    /*
     * DLC
     */
    uint8_t dlc =
        message->data_length_code;


    if (dlc > 8)
    {
        dlc = 8;
    }


    output[position++] =
        int_to_hex(
            dlc
        );


    /*
     * Dados
     */
    if (!message->rtr)
    {
        for (
            int i = 0;
            i < dlc;
            i++
        )
        {
            output[position++] =
                int_to_hex(
                    message->data[i] >> 4
                );


            output[position++] =
                int_to_hex(
                    message->data[i]
                );
        }
    }

    /*
 * Timestamp LAWICEL
 *
 * 0000 ... EA5F
 * unidade = 1 ms
 * rollover = 60000 ms
 */
if (timestamp_enabled)
{
    uint16_t timestamp =
        (uint16_t)(
            (
                esp_timer_get_time()
                / 1000ULL
            )
            % 60000ULL
        );


    output[position++] =
        int_to_hex(
            (timestamp >> 12) &
            0x0F
        );

    output[position++] =
        int_to_hex(
            (timestamp >> 8) &
            0x0F
        );

    output[position++] =
        int_to_hex(
            (timestamp >> 4) &
            0x0F
        );

    output[position++] =
        int_to_hex(
            timestamp &
            0x0F
        );
}


    /*
     * Final LAWICEL
     */
    output[position++] =
        '\r';


    usb_write_data(
        output,
        position
    );
}


/* ============================================================
 * TASK RX CAN
 * ============================================================ */

static void can_rx_task(
    void *arg
)
{
    twai_message_t message;


    while (1)
    {
        if (
            !can_twai_is_open()
        )
        {
            vTaskDelay(
                pdMS_TO_TICKS(10)
            );

            continue;
        }


        esp_err_t result =
            can_twai_receive(
                &message,
                pdMS_TO_TICKS(20)
            );


        if (
            result == ESP_OK
        )
        {
            send_can_frame_to_usb(
                &message
            );
        }
    }
}


/* ============================================================
 * COMANDOS SLCAN
 * ============================================================ */

static void process_command(
    const char *command
)
{
    if (
        command == NULL ||
        command[0] == '\0'
    )
    {
        return;
    }


    /*
     * Frames CAN
     */
    if (
        command[0] == 't' ||
        command[0] == 'T' ||
        command[0] == 'r' ||
        command[0] == 'R'
    )
    {
        process_can_transmit(
            command
        );

        return;
    }


    /*
     * Hardware version
     */
    if (
        strcmp(
            command,
            "V"
        ) == 0
    )
    {
        usb_write_string(
            "V0100\r"
        );

        return;
    }


    /*
     * Firmware version
     */
    if (
        strcmp(
            command,
            "v"
        ) == 0
    )
    {
        usb_write_string(
            "v0100\r"
        );

        return;
    }


    /*
     * Serial
     */
    if (
        strcmp(
            command,
            "N"
        ) == 0
    )
    {
        usb_write_string(
            "N0001\r"
        );

        return;
    }


    /*
     * Status
     */
if (
    strcmp(
        command,
        "F"
    ) == 0
)
{
    uint8_t flags =
        can_twai_get_slcan_status();

    char response[4];

    response[0] = 'F';

    response[1] =
        int_to_hex(
            flags >> 4
        );

    response[2] =
        int_to_hex(
            flags
        );

    response[3] = '\r';

    usb_write_data(
        response,
        sizeof(response)
    );

    return;
}


    /*
     * Close CAN
     */
    if (
        strcmp(
            command,
            "C"
        ) == 0
    )
    {
        if (
            can_twai_close()
            == ESP_OK
        )
        {
            slcan_ok();
        }
        else
        {
            slcan_error();
        }

        return;
    }


    /*
     * Open CAN
     */
    if (
        strcmp(
            command,
            "O"
        ) == 0
    )
    {
        if (
            can_twai_open(
                false
            ) == ESP_OK
        )
        {
            slcan_ok();
        }
        else
        {
            slcan_error();
        }

        return;
    }


    /*
     * Listen only
     */
    if (
        strcmp(
            command,
            "L"
        ) == 0
    )
    {
        if (
            can_twai_open(
                true
            ) == ESP_OK
        )
        {
            slcan_ok();
        }
        else
        {
            slcan_error();
        }

        return;
    }


    /*
     * USBtin da empresa:
     *
     * 250 kbit/s
     */
    if (
        strcmp(
            command,
            "s43A301"
        ) == 0
    )
    {
        if (
            can_twai_set_bitrate(
                250000
            ) == ESP_OK
        )
        {
            slcan_ok();
        }
        else
        {
            slcan_error();
        }

        return;
    }


    /*
     * Bitrates padrão SLCAN
     */
    if (
        command[0] == 'S' &&
        command[1] >= '0' &&
        command[1] <= '8' &&
        command[2] == '\0'
    )
    {
        static const uint32_t bitrates[] =
        {
            10000,
            20000,
            50000,
            100000,
            125000,
            250000,
            500000,
            800000,
            1000000
        };


        uint32_t bitrate =
            bitrates[
                command[1] - '0'
            ];


        if (
            can_twai_set_bitrate(
                bitrate
            ) == ESP_OK
        )
        {
            slcan_ok();
        }
        else
        {
            slcan_error();
        }


        return;
    }


    /*
     * Timestamp OFF
     *
     * Por enquanto estamos sem timestamp.
     */
if (
    strcmp(
        command,
        "Z0"
    ) == 0
)
{
    timestamp_enabled = false;

    slcan_ok();

    return;
}


    /*
     * Timestamp ainda não implementado.
     */
if (
    strcmp(
        command,
        "Z1"
    ) == 0
)
{
    timestamp_enabled = true;

    slcan_ok();

    return;
}


    /*
     * Comando desconhecido
     */
    slcan_error();
}


/* ============================================================
 * TASK USB
 * ============================================================ */

static void usb_slcan_task(
    void *arg
)
{
    uint8_t rx_buffer[64];


    command_position = 0;


    memset(
        command_buffer,
        0,
        sizeof(command_buffer)
    );


    while (1)
    {
        int received =
            usb_serial_jtag_read_bytes(
                rx_buffer,
                sizeof(rx_buffer),
                pdMS_TO_TICKS(100)
            );


        if (
            received <= 0
        )
        {
            continue;
        }


        for (
            int i = 0;
            i < received;
            i++
        )
        {
            char c =
                (char)rx_buffer[i];


            if (
                c == '\r'
            )
            {
                command_buffer[
                    command_position
                ] = '\0';


                if (
                    command_position > 0
                )
                {
                    process_command(
                        command_buffer
                    );
                }


                command_position = 0;


                memset(
                    command_buffer,
                    0,
                    sizeof(command_buffer)
                );


                continue;
            }


            /*
             * Ignora LF
             */
            if (
                c == '\n'
            )
            {
                continue;
            }


            if (
                command_position <
                (
                    SLCAN_COMMAND_SIZE -
                    1
                )
            )
            {
                command_buffer[
                    command_position++
                ] = c;
            }
            else
            {
                command_position = 0;


                memset(
                    command_buffer,
                    0,
                    sizeof(command_buffer)
                );


                slcan_error();
            }
        }
    }
}


/* ============================================================
 * INIT
 * ============================================================ */

void usb_slcan_init(void)
{
    usb_serial_jtag_driver_config_t config = {
        .rx_buffer_size =
            USB_RX_BUFFER_SIZE,

        .tx_buffer_size =
            USB_TX_BUFFER_SIZE
    };


    ESP_ERROR_CHECK(
        usb_serial_jtag_driver_install(
            &config
        )
    );


    usb_tx_mutex =
        xSemaphoreCreateMutex();


    ESP_ERROR_CHECK(
        usb_tx_mutex != NULL
        ? ESP_OK
        : ESP_ERR_NO_MEM
    );


    BaseType_t result;


    result =
        xTaskCreate(
            usb_slcan_task,
            "usb_slcan",
            4096,
            NULL,
            10,
            NULL
        );


    ESP_ERROR_CHECK(
        result == pdPASS
        ? ESP_OK
        : ESP_FAIL
    );


    result =
        xTaskCreate(
            can_rx_task,
            "can_rx",
            4096,
            NULL,
            9,
            NULL
        );


    ESP_ERROR_CHECK(
        result == pdPASS
        ? ESP_OK
        : ESP_FAIL
    );
}