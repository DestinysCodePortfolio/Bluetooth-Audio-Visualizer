
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/multicore.h"
#include "hardware/spi.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"

#include "btstack.h"
#include "classic/btstack_sbc.h"

#include "spi_display.h"
#include "lv_conf.h"
#include "lvgl_oscilloscope.h"
#include "audio_buffer.h"
#include <lvgl.h>

// ── BTstack state ──────────────────────────────────────────────────────────
static btstack_packet_callback_registration_t hci_event_cb;
static uint8_t sdp_a2dp_sink_buffer[220];
static uint8_t sdp_avrcp_buffer[220];

static uint8_t media_sbc_codec_capabilities[] = {
    (AVDTP_SBC_44100 << 4) | AVDTP_SBC_STEREO,
    (AVDTP_SBC_BLOCK_LENGTH_16 << 4) | (AVDTP_SBC_SUBBANDS_8 << 2)
        | AVDTP_SBC_ALLOCATION_METHOD_LOUDNESS,
    2, 53
};
static uint8_t media_sbc_codec_configuration[4];
static avdtp_stream_endpoint_t *local_stream_endpoint;
static btstack_sbc_decoder_state_t sbc_decoder_state;

static volatile uint32_t total_frames_decoded = 0;

// ── PWM Audio ─────────────────────────────────────────────────────────────
#define AUDIO_PIN_L 15
#define AUDIO_PIN_R 14

// GP14 and GP15 share PWM slice 7, channels B and A respectively
// wrap = 125,000,000 / 44100 = 2834
#define PWM_WRAP 2834

void audio_init(void) {
    gpio_set_function(AUDIO_PIN_L, GPIO_FUNC_PWM);
    gpio_set_function(AUDIO_PIN_R, GPIO_FUNC_PWM);

    uint slice = pwm_gpio_to_slice_num(AUDIO_PIN_L); // same slice for both

    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_clkdiv(&cfg, 1.0f);
    pwm_config_set_wrap(&cfg, PWM_WRAP);
    pwm_init(slice, &cfg, true);

    // Start at midpoint (silence)
    pwm_set_gpio_level(AUDIO_PIN_L, PWM_WRAP / 2);
    pwm_set_gpio_level(AUDIO_PIN_R, PWM_WRAP / 2);
}

// ── LVGL helpers ───────────────────────────────────────────────────────────
uint32_t cs122_get_millis(void) {
    return to_ms_since_boot(get_absolute_time());
}

void cs122_flush_cb_partial(lv_display_t *disp, const lv_area_t *area,
                             uint8_t *px_buf)
{
    ucr::bcoe::SPIDisplay *spi =
        reinterpret_cast<ucr::bcoe::SPIDisplay *>(lv_display_get_user_data(disp));
    spi->drawBitmap(2 * area->x1, area->y1,
                    2 * area->x2 + 1, area->y2, px_buf);
    lv_display_flush_ready(disp);
}

// ── Core 1: LVGL display loop ──────────────────────────────────────────────
static ucr::bcoe::SPIDisplay *g_spi_display = nullptr;

void core1_entry(void) {
    while (g_spi_display == nullptr) tight_loop_contents();

    ucr::bcoe::cs::cs122::LVGL_Oscilloscope app(
        g_spi_display, cs122_flush_cb_partial, cs122_get_millis);
    app.run();
}

// ── SBC decode callback ────────────────────────────────────────────────────
static void sbc_decoded_handler(int16_t *pcm_data, int num_audio_frames,
                                int num_channels, int sample_rate, void *ctx)
{
    (void)ctx; (void)sample_rate;

    for (int i = 0; i < num_audio_frames; i++) {
        int16_t left  = pcm_data[i * num_channels];
        int16_t right = (num_channels > 1) ? pcm_data[i * num_channels + 1] : left;

        // Map signed 16-bit → 0..PWM_WRAP
        uint32_t l_level = (uint32_t)((left  + 32768) * PWM_WRAP / 65535);
        uint32_t r_level = (uint32_t)((right + 32768) * PWM_WRAP / 65535);

        pwm_set_gpio_level(AUDIO_PIN_L, l_level);
        pwm_set_gpio_level(AUDIO_PIN_R, r_level);
    }

    audio_buf_push(pcm_data, (uint32_t)num_audio_frames, AUDIO_SRC_BLUETOOTH);
    total_frames_decoded++;
}

// ── A2DP media handler ─────────────────────────────────────────────────────
static void a2dp_media_handler(uint8_t seid, uint8_t *packet, uint16_t size) {
    (void)seid;
    if (size < 13) return;
    btstack_sbc_decoder_process_data(&sbc_decoder_state, 0,
                                     packet + 13, size - 13);
}

// ── HCI / A2DP event handler ───────────────────────────────────────────────
static void packet_handler(uint8_t packet_type, uint16_t channel,
                           uint8_t *packet, uint16_t size)
{
    UNUSED(channel); UNUSED(size);
    if (packet_type != HCI_EVENT_PACKET) return;

    switch (hci_event_packet_get_type(packet)) {
        case BTSTACK_EVENT_STATE:
            if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING) {
                printf("BT ready — pair as BTVisualizer\n");
                gap_discoverable_control(1);
                gap_connectable_control(1);
            }
            break;
        case HCI_EVENT_PIN_CODE_REQUEST: {
            bd_addr_t addr;
            hci_event_pin_code_request_get_bd_addr(packet, addr);
            gap_pin_code_response(addr, "0000");
            break;
        }
        case HCI_EVENT_USER_CONFIRMATION_REQUEST: {
            bd_addr_t addr;
            hci_event_user_confirmation_request_get_bd_addr(packet, addr);
            gap_ssp_confirmation_response(addr);
            break;
        }
        case HCI_EVENT_CONNECTION_COMPLETE:
            printf("Connected, status: %d\n",
                   hci_event_connection_complete_get_status(packet));
            break;
        case HCI_EVENT_DISCONNECTION_COMPLETE:
            printf("Disconnected\n");
            audio_buf_stop();
            // Return to silence on disconnect
            pwm_set_gpio_level(AUDIO_PIN_L, PWM_WRAP / 2);
            pwm_set_gpio_level(AUDIO_PIN_R, PWM_WRAP / 2);
            break;
        case HCI_EVENT_AVDTP_META: {
            uint8_t sub = hci_event_avdtp_meta_get_subevent_code(packet);
            if (sub == AVDTP_SUBEVENT_STREAMING_CONNECTION_RELEASED ||
                sub == AVDTP_SUBEVENT_SIGNALING_CONNECTION_RELEASED)
                audio_buf_stop();
            break;
        }
        default:
            break;
    }
}

// ── Core 0: main ───────────────────────────────────────────────────────────
int main(void) {
    stdio_init_all();
    sleep_ms(500);

    if (cyw43_arch_init()) {
        printf("CYW43 init failed\n");
        return -1;
    }
    adc_init();
    audio_init();  // ← PWM init before BTstack starts

    static ucr::bcoe::SPIDisplay spi_display(480, 272, 10000000, 20);
    spi_display.begin();
    spi_display.clear();

    audio_buf_init();

    g_spi_display = &spi_display;
    multicore_launch_core1(core1_entry);

    l2cap_init();
    sdp_init();

    btstack_sbc_decoder_init(&sbc_decoder_state, SBC_MODE_STANDARD,
                             sbc_decoded_handler, NULL);

    a2dp_sink_init();
    a2dp_sink_register_packet_handler(&packet_handler);
    a2dp_sink_register_media_handler(&a2dp_media_handler);

    avdtp_stream_endpoint_t *ep =
        a2dp_sink_create_stream_endpoint(
            AVDTP_AUDIO, AVDTP_CODEC_SBC,
            media_sbc_codec_capabilities, sizeof(media_sbc_codec_capabilities),
            media_sbc_codec_configuration, sizeof(media_sbc_codec_configuration));
    local_stream_endpoint = ep;

    memset(sdp_a2dp_sink_buffer, 0, sizeof(sdp_a2dp_sink_buffer));
    a2dp_sink_create_sdp_record(sdp_a2dp_sink_buffer,
                                sdp_create_service_record_handle(),
                                AVDTP_SINK_FEATURE_MASK_HEADPHONE,
                                NULL, NULL);
    sdp_register_service(sdp_a2dp_sink_buffer);

    memset(sdp_avrcp_buffer, 0, sizeof(sdp_avrcp_buffer));
    avrcp_controller_create_sdp_record(sdp_avrcp_buffer,
                                       sdp_create_service_record_handle(),
                                       1, NULL, NULL);
    sdp_register_service(sdp_avrcp_buffer);

    gap_set_local_name("BTVisualizer");
    gap_set_class_of_device(0x240404);
    gap_set_default_link_policy_settings(LM_LINK_POLICY_ENABLE_SNIFF_MODE);

    hci_event_cb.callback = &packet_handler;
    hci_add_event_handler(&hci_event_cb);

    hci_power_control(HCI_POWER_ON);

    printf("BTVisualizer running — pair your phone\n");
    btstack_run_loop_execute();

    return 0;
}