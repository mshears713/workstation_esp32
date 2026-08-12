/*
 * ir_roku -- Roku TV IR transmit for the ESP32-S3-BOX-3 + SENSOR accessory.
 * SPDX-License-Identifier: CC0-1.0
 *
 * =====================================================================
 * HARDWARE FINDINGS  (Phase 0; all verified against the real board)
 * =====================================================================
 *
 * Three pins matter, and only one of them is documented anywhere obvious.
 * The esp-box-3 BSP header defines NO IR symbols at all -- it lists GPIO39
 * merely as BSP_PMOD1_IO3 -- so the schematic is the source of truth:
 * espressif/esp-box, hardware/SCH_ESP32-S3-BOX-3_V1.0,
 * SCH_ESP32-S3-BOX-3-SENSOR-02_V1.1_20230808.pdf.
 *
 * IR_TX_GPIO = GPIO39. Sheet block "IR Transmitter":
 *   IO39 -> net IR_TX -> R4 4.7K -> base of Q1 (L8050 NPN); Q1 collector
 *   drives IR3 (IR67-21C IR LED) through R3 30R up to the IR_3V3 rail.
 *   NPN base drive means GPIO high = LED on, which is RMT's default output
 *   polarity -- no invert_out needed.
 *
 * IR_RX_GPIO = GPIO38. Same sheet, "IR Receiver": IRM-H638T demodulator
 *   output -> net IR_RX -> IO38. Used here only for the loopback self-test.
 *
 * IR_PWR_EN_GPIO = GPIO44, ACTIVE LOW. This is the one that will waste your
 *   afternoon. Also in the "IR Receiver" block: VCC_3V3 feeds Q2 (AO3401A,
 *   P-channel MOSFET) whose drain is the IR_3V3 rail, and Q2's gate is driven
 *   by the RXD net through R12 10K with R11 10K holding it up. IR_3V3 powers
 *   BOTH the receiver AND the emitter LED's anode, so nothing works unless
 *   RXD is pulled LOW. RXD crosses the goldfinger to the BOX as U0RXD, which
 *   on the ESP32-S3 is fixed-function GPIO44.
 *     Corroboration that this is deliberate design and not a misread: the
 *     SENSOR-01 sheet gates SD card power (SD_VCC33) with an identical
 *     active-low AO3401A driven by TXD/GPIO43. Same topology, twice.
 *     Consequence: with the default UART0 console owning GPIO44 it idles
 *     HIGH, IR_3V3 stays dead, and GPIO39 cheerfully toggles a transistor
 *     feeding an unpowered LED. RMT reports every frame transmitted
 *     successfully and not one photon leaves the board. That is why
 *     sdkconfig.defaults moves the console to USB Serial/JTAG.
 *
 * The signals reach the BOX through the SENSOR-01 "Goldfinger CONN"
 * (PCIE_CONN_36P J2). Flash over the BOX's USB-C with the BOX docked.
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_rx.h"
#include "driver/rmt_encoder.h"
#include "ir_nec_encoder.h"
#include "ir_roku.h"

static const char *TAG = "ir_roku";

#define IR_TX_GPIO        (39)
#define IR_RX_GPIO        (38)
#define IR_PWR_EN_GPIO    (44)
#define IR_PWR_ON_LEVEL   (0)

/* NEC carrier is 38 kHz. RMT resolution 1 MHz => 1 tick = 1 us. */
#define IR_RESOLUTION_HZ  (1000000)
#define NEC_CARRIER_HZ    (38000)

/*
 * The IDF NEC encoder transmits .command as a raw 16-bit LSB-first field --
 * it does NOT derive the inverse byte for us (see ir_nec_encoder.c, which
 * just hands both halves to the bytes encoder). A standard NEC command word
 * is therefore the byte followed by its complement.
 */
#define NEC_CMD_WORD(c)   ((uint16_t)((((uint16_t)(uint8_t)~(c)) << 8) | (uint8_t)(c)))

static rmt_channel_handle_t s_tx_channel;
static rmt_channel_handle_t s_rx_channel;
static rmt_encoder_handle_t s_nec_encoder;
static QueueHandle_t s_rx_queue;

static const rmt_transmit_config_t s_tx_cfg = {
    .loop_count = 0,   /* one frame per press; no hardware repeat */
};
static const rmt_receive_config_t s_rx_cfg = {
    .signal_range_min_ns = 1250,
    .signal_range_max_ns = 12000000,
};

/* ---- minimal NEC decode, for the loopback self-test only ---- */
#define NEC_DECODE_MARGIN 200

static bool nec_in_range(uint32_t sig, uint32_t spec)
{
    return (sig < spec + NEC_DECODE_MARGIN) && (sig > spec - NEC_DECODE_MARGIN);
}

static bool nec_bit(rmt_symbol_word_t *s, bool *out)
{
    if (nec_in_range(s->duration0, 560) && nec_in_range(s->duration1, 1690)) {
        *out = true;
        return true;
    }
    if (nec_in_range(s->duration0, 560) && nec_in_range(s->duration1, 560)) {
        *out = false;
        return true;
    }
    return false;
}

static bool nec_decode(rmt_symbol_word_t *sym, size_t n, uint16_t *addr, uint16_t *cmd)
{
    if (n < 34) {
        return false;
    }
    if (!nec_in_range(sym->duration0, 9000) || !nec_in_range(sym->duration1, 4500)) {
        return false;
    }
    rmt_symbol_word_t *cur = sym + 1;
    uint16_t a = 0, c = 0;
    bool bit;
    for (int i = 0; i < 16; i++, cur++) {
        if (!nec_bit(cur, &bit)) {
            return false;
        }
        if (bit) {
            a |= 1 << i;
        }
    }
    for (int i = 0; i < 16; i++, cur++) {
        if (!nec_bit(cur, &bit)) {
            return false;
        }
        if (bit) {
            c |= 1 << i;
        }
    }
    *addr = a;
    *cmd = c;
    return true;
}

static bool IRAM_ATTR rx_done_cb(rmt_channel_handle_t ch,
                                 const rmt_rx_done_event_data_t *edata, void *ctx)
{
    BaseType_t hp = pdFALSE;
    xQueueSendFromISR((QueueHandle_t)ctx, edata, &hp);
    return hp == pdTRUE;
}

esp_err_t roku_ir_init(void)
{
    /* Power the IR_3V3 rail FIRST. Emitter and receiver are both dead
     * without it, and RMT will happily pretend to transmit regardless. */
    gpio_config_t pwr_cfg = {
        .pin_bit_mask = 1ULL << IR_PWR_EN_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&pwr_cfg), TAG, "IR power gpio config failed");
    ESP_RETURN_ON_ERROR(gpio_set_level(IR_PWR_EN_GPIO, IR_PWR_ON_LEVEL), TAG, "IR power set failed");
    vTaskDelay(pdMS_TO_TICKS(50));   /* let the rail and receiver settle */
    ESP_LOGI(TAG, "IR_3V3 rail enabled (GPIO%d driven %d)", IR_PWR_EN_GPIO, IR_PWR_ON_LEVEL);

    rmt_tx_channel_config_t tx_channel_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = IR_RESOLUTION_HZ,
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
        .gpio_num = IR_TX_GPIO,
    };
    ESP_RETURN_ON_ERROR(rmt_new_tx_channel(&tx_channel_cfg, &s_tx_channel), TAG, "tx channel failed");

    /* 38 kHz carrier at 33% duty -- only the marks get modulated. */
    rmt_carrier_config_t carrier_cfg = {
        .duty_cycle = 0.33,
        .frequency_hz = NEC_CARRIER_HZ,
    };
    ESP_RETURN_ON_ERROR(rmt_apply_carrier(s_tx_channel, &carrier_cfg), TAG, "carrier failed");

    ir_nec_encoder_config_t nec_encoder_cfg = { .resolution = IR_RESOLUTION_HZ };
    ESP_RETURN_ON_ERROR(rmt_new_ir_nec_encoder(&nec_encoder_cfg, &s_nec_encoder), TAG, "encoder failed");

    rmt_rx_channel_config_t rx_channel_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = IR_RESOLUTION_HZ,
        .mem_block_symbols = 64,
        .gpio_num = IR_RX_GPIO,
    };
    ESP_RETURN_ON_ERROR(rmt_new_rx_channel(&rx_channel_cfg, &s_rx_channel), TAG, "rx channel failed");

    s_rx_queue = xQueueCreate(1, sizeof(rmt_rx_done_event_data_t));
    ESP_RETURN_ON_FALSE(s_rx_queue, ESP_ERR_NO_MEM, TAG, "rx queue failed");
    rmt_rx_event_callbacks_t rx_cbs = { .on_recv_done = rx_done_cb };
    ESP_RETURN_ON_ERROR(rmt_rx_register_event_callbacks(s_rx_channel, &rx_cbs, s_rx_queue),
                        TAG, "rx callbacks failed");

    ESP_RETURN_ON_ERROR(rmt_enable(s_tx_channel), TAG, "tx enable failed");
    ESP_RETURN_ON_ERROR(rmt_enable(s_rx_channel), TAG, "rx enable failed");

    ESP_LOGI(TAG, "ready: TX=GPIO%d RX=GPIO%d addr=0x%04X carrier=%dHz",
             IR_TX_GPIO, IR_RX_GPIO, ROKU_NEC_ADDRESS, NEC_CARRIER_HZ);
    return ESP_OK;
}

static esp_err_t send_frame(uint16_t address, uint8_t cmd)
{
    const ir_nec_scan_code_t frame = {
        .address = address,
        .command = NEC_CMD_WORD(cmd),
    };
    ESP_RETURN_ON_ERROR(rmt_transmit(s_tx_channel, s_nec_encoder, &frame,
                                     sizeof(frame), &s_tx_cfg), TAG, "transmit failed");
    return rmt_tx_wait_all_done(s_tx_channel, portMAX_DELAY);
}

esp_err_t roku_ir_send(uint8_t cmd)
{
    return send_frame(ROKU_NEC_ADDRESS, cmd);
}

/*
 * Deliberately not the Roku address. The loopback proves our own emitter,
 * carrier and encoder -- it says nothing about the TV -- so there is no
 * reason to poke the TV every time we boot. 0x0000 is not a Roku address,
 * so the panel ignores the frame while our receiver still hears it.
 */
#define SELFTEST_ADDRESS  (0x0000)
#define SELFTEST_COMMAND  (0x5A)

bool roku_ir_selftest(void)
{
    rmt_symbol_word_t rx_symbols[64];
    rmt_rx_done_event_data_t rx_data;

    /* Arm the receiver before transmitting so we can catch our own echo. */
    if (rmt_receive(s_rx_channel, rx_symbols, sizeof(rx_symbols), &s_rx_cfg) != ESP_OK) {
        return false;
    }
    if (send_frame(SELFTEST_ADDRESS, SELFTEST_COMMAND) != ESP_OK) {
        return false;
    }
    if (xQueueReceive(s_rx_queue, &rx_data, pdMS_TO_TICKS(200)) != pdPASS) {
        ESP_LOGW(TAG, "self-test: nothing heard back on GPIO%d", IR_RX_GPIO);
        return false;
    }

    uint16_t addr = 0, cmd = 0;
    if (!nec_decode(rx_data.received_symbols, rx_data.num_symbols, &addr, &cmd)) {
        ESP_LOGW(TAG, "self-test: IR seen but not a clean NEC frame (%d symbols)",
                 (int)rx_data.num_symbols);
        return false;
    }

    ESP_LOGI(TAG, "self-test PASSED: heard back addr=0x%04X cmd=0x%04X", addr, cmd);
    return addr == SELFTEST_ADDRESS && (cmd & 0xFF) == SELFTEST_COMMAND;
}

const char *roku_key_name(uint8_t cmd)
{
    switch (cmd) {
    case ROKU_KEY_POWER:      return "Power";
    case ROKU_KEY_HOME:       return "Home";
    case ROKU_KEY_BACK:       return "Back";
    case ROKU_KEY_UP:         return "Up";
    case ROKU_KEY_DOWN:       return "Down";
    case ROKU_KEY_LEFT:       return "Left";
    case ROKU_KEY_RIGHT:      return "Right";
    case ROKU_KEY_OK:         return "OK";
    case ROKU_KEY_REPLAY:     return "Replay";
    case ROKU_KEY_STAR:       return "Star/Options";
    case ROKU_KEY_REWIND:     return "Rewind";
    case ROKU_KEY_PLAY_PAUSE: return "Play/Pause";
    case ROKU_KEY_FORWARD:    return "Forward";
    case ROKU_KEY_VOL_UP:     return "Vol+";
    case ROKU_KEY_VOL_DOWN:   return "Vol-";
    case ROKU_KEY_MUTE:       return "Mute";
    case ROKU_KEY_SLEEP:      return "Sleep";
    case ROKU_KEY_POWER_ALT:  return "Power(alt 0x97)";
    default:                  return "?";
    }
}
