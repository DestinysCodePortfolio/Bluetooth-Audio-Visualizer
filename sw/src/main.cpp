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

// ─────────────────────────────────────────────────────────────
// BTstack state
// ─────────────────────────────────────────────────────────────

static btstack_packet_callback_registration_t hci_event_cb;

static uint8_t sdp_a2dp_sink_buffer[220];
static uint8_t sdp_avrcp_buffer[220];

static uint8_t media_sbc_codec_capabilities[] = {
    (AVDTP_SBC_44100 << 4) | AVDTP_SBC_STEREO,
    (AVDTP_SBC_BLOCK_LENGTH_16 << 4) |
    (AVDTP_SBC_SUBBANDS_8 << 2) |
    AVDTP_SBC_ALLOCATION_METHOD_LOUDNESS,
    2,
    53
};

static uint8_t media_sbc_codec_configuration[4];

static avdtp_stream_endpoint_t *local_stream_endpoint;

static btstack_sbc_decoder_state_t sbc_decoder_state;

static volatile uint32_t total_frames_decoded = 0;

// ─────────────────────────────────────────────────────────────
// PWM Audio Output — ring buffer + repeating timer
// ─────────────────────────────────────────────────────────────

#define AUDIO_PIN_R  14
#define PWM_WRAP     255
#define VOLUME_DIV   4

// Ring buffer (power of 2 size for fast masking)
#define RING_SIZE    4096
#define RING_MASK    (RING_SIZE - 1)

static uint8_t  ring_buf[RING_SIZE];
static volatile uint32_t ring_head = 0;
static volatile uint32_t ring_tail = 0;

static inline void ring_push(uint8_t val) {
    uint32_t next = (ring_head + 1) & RING_MASK;
    if (next == ring_tail) return; // drop sample if full
    ring_buf[ring_head] = val;
    ring_head = next;
}

static inline uint8_t ring_pop(void) {
    if (ring_tail == ring_head) return PWM_WRAP / 2; // silence
    uint8_t v = ring_buf[ring_tail];
    ring_tail = (ring_tail + 1) & RING_MASK;
    return v;
}

// Timer callback — fires at 44100 Hz, outputs one sample
static bool audio_timer_cb(struct repeating_timer *t) {
    (void)t;
    pwm_set_gpio_level(AUDIO_PIN_R, ring_pop());
    return true;
}

static struct repeating_timer audio_timer;

void audio_init(void) {
    gpio_set_function(AUDIO_PIN_R, GPIO_FUNC_PWM);

    uint slice = pwm_gpio_to_slice_num(AUDIO_PIN_R);

    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_clkdiv(&cfg, 1.0f);
    pwm_config_set_wrap(&cfg, PWM_WRAP);
    pwm_init(slice, &cfg, true);

    // Start at silence midpoint
    pwm_set_gpio_level(AUDIO_PIN_R, PWM_WRAP / 2);

    // Repeating timer at ~44100 Hz (22.676 us per sample)
    add_repeating_timer_us(-23, audio_timer_cb, NULL, &audio_timer);
}

// ─────────────────────────────────────────────────────────────
// LVGL Helpers
// ─────────────────────────────────────────────────────────────

uint32_t cs122_get_millis(void) {
    return to_ms_since_boot(get_absolute_time());
}

void cs122_flush_cb_partial(lv_display_t *disp,
                            const lv_area_t *area,
                            uint8_t *px_buf)
{
    ucr::bcoe::SPIDisplay *spi =
        reinterpret_cast<ucr::bcoe::SPIDisplay *>(
            lv_display_get_user_data(disp));

    spi->drawBitmap(
        2 * area->x1,
        area->y1,
        2 * area->x2 + 1,
        area->y2,
        px_buf
    );

    lv_display_flush_ready(disp);
}

// ─────────────────────────────────────────────────────────────
// Core 1: LVGL Task
// ─────────────────────────────────────────────────────────────

static ucr::bcoe::SPIDisplay *g_spi_display = nullptr;

void core1_entry(void) {

    while (g_spi_display == nullptr) {
        tight_loop_contents();
    }

    ucr::bcoe::cs::cs122::LVGL_Oscilloscope app(
        g_spi_display,
        cs122_flush_cb_partial,
        cs122_get_millis
    );

    app.run();
}

// ─────────────────────────────────────────────────────────────
// SBC Decode Callback
// ─────────────────────────────────────────────────────────────

static void sbc_decoded_handler(int16_t *pcm_data,
                                int num_audio_frames,
                                int num_channels,
                                int sample_rate,
                                void *ctx)
{
    (void)ctx;

    for (int i = 0; i < num_audio_frames; i++) {

        int16_t left  = pcm_data[i * num_channels];
        int16_t right = (num_channels > 1)
                            ? pcm_data[i * num_channels + 1]
                            : left;

        // Mix stereo -> mono
        int32_t mixed = ((int32_t)left + right) / 2;

        // Reduce volume
        mixed /= VOLUME_DIV;

        // Clamp
        if (mixed >  32767) mixed =  32767;
        if (mixed < -32768) mixed = -32768;

        // Convert signed PCM -> unsigned PWM level and push to ring buffer
        uint8_t pwm_level = (uint8_t)(
            ((mixed + 32768) * PWM_WRAP) / 65535
        );

        ring_push(pwm_level);
    }

    // Feed oscilloscope buffer
    audio_buf_push(
        pcm_data,
        (uint32_t)num_audio_frames,
        AUDIO_SRC_BLUETOOTH
    );

    total_frames_decoded++;

    if (total_frames_decoded % 100 == 0) {
        printf(
            "[audio] frames=%lu rate=%d ch=%d buf=%lu\n",
            (unsigned long)total_frames_decoded,
            sample_rate,
            num_channels,
            (unsigned long)((ring_head - ring_tail) & RING_MASK)
        );
    }
}

// ─────────────────────────────────────────────────────────────
// A2DP Media Handler
// ─────────────────────────────────────────────────────────────

static void a2dp_media_handler(uint8_t seid,
                               uint8_t *packet,
                               uint16_t size)
{
    (void)seid;

    // Skip RTP header
    if (size < 13) return;

    btstack_sbc_decoder_process_data(
        &sbc_decoder_state,
        0,
        packet + 13,
        size - 13
    );
}

// ─────────────────────────────────────────────────────────────
// BTstack Packet Handler
// ─────────────────────────────────────────────────────────────

static void packet_handler(uint8_t packet_type,
                           uint16_t channel,
                           uint8_t *packet,
                           uint16_t size)
{
    UNUSED(channel);
    UNUSED(size);

    if (packet_type != HCI_EVENT_PACKET) return;

    switch (hci_event_packet_get_type(packet)) {

        case BTSTACK_EVENT_STATE:

            if (btstack_event_state_get_state(packet)
                == HCI_STATE_WORKING)
            {
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

            printf(
                "Connected, status: %d\n",
                hci_event_connection_complete_get_status(packet)
            );

            break;

        case HCI_EVENT_DISCONNECTION_COMPLETE:

            printf("Disconnected\n");

            audio_buf_stop();

            // Drain ring buffer and silence output
            ring_head = ring_tail = 0;
            pwm_set_gpio_level(AUDIO_PIN_R, PWM_WRAP / 2);

            break;

        case HCI_EVENT_AVDTP_META: {

            uint8_t sub =
                hci_event_avdtp_meta_get_subevent_code(packet);

            if (sub == AVDTP_SUBEVENT_STREAMING_CONNECTION_RELEASED ||
                sub == AVDTP_SUBEVENT_SIGNALING_CONNECTION_RELEASED)
            {
                audio_buf_stop();
                ring_head = ring_tail = 0;
            }

            break;
        }

        default:
            break;
    }
}

// ─────────────────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────────────────

int main(void) {

    stdio_init_all();

    sleep_ms(500);

    // CYW43 init
    if (cyw43_arch_init()) {
        printf("CYW43 init failed\n");
        return -1;
    }

    adc_init();

    audio_init();

    // Display
    static ucr::bcoe::SPIDisplay spi_display(
        480,
        272,
        10000000,
        20
    );

    spi_display.begin();
    spi_display.clear();

    audio_buf_init();

    // Launch LVGL core
    g_spi_display = &spi_display;
    multicore_launch_core1(core1_entry);

    // BTstack init
    l2cap_init();
    sdp_init();

    btstack_sbc_decoder_init(
        &sbc_decoder_state,
        SBC_MODE_STANDARD,
        sbc_decoded_handler,
        NULL
    );

    a2dp_sink_init();
    a2dp_sink_register_packet_handler(&packet_handler);
    a2dp_sink_register_media_handler(&a2dp_media_handler);

    avdtp_stream_endpoint_t *ep =
        a2dp_sink_create_stream_endpoint(
            AVDTP_AUDIO,
            AVDTP_CODEC_SBC,
            media_sbc_codec_capabilities,
            sizeof(media_sbc_codec_capabilities),
            media_sbc_codec_configuration,
            sizeof(media_sbc_codec_configuration)
        );

    local_stream_endpoint = ep;

    // SDP A2DP
    memset(sdp_a2dp_sink_buffer, 0, sizeof(sdp_a2dp_sink_buffer));
    a2dp_sink_create_sdp_record(
        sdp_a2dp_sink_buffer,
        sdp_create_service_record_handle(),
        AVDTP_SINK_FEATURE_MASK_HEADPHONE,
        NULL,
        NULL
    );
    sdp_register_service(sdp_a2dp_sink_buffer);

    // SDP AVRCP
    memset(sdp_avrcp_buffer, 0, sizeof(sdp_avrcp_buffer));
    avrcp_controller_create_sdp_record(
        sdp_avrcp_buffer,
        sdp_create_service_record_handle(),
        1,
        NULL,
        NULL
    );
    sdp_register_service(sdp_avrcp_buffer);

    // Device name & BT settings
    gap_set_local_name("BTVisualizer");
    gap_set_class_of_device(0x240404);
    gap_set_default_link_policy_settings(
        LM_LINK_POLICY_ENABLE_SNIFF_MODE
    );

    // HCI event callback
    hci_event_cb.callback = &packet_handler;
    hci_add_event_handler(&hci_event_cb);

    // Power on Bluetooth
    hci_power_control(HCI_POWER_ON);

    printf("BTVisualizer running — pair your phone\n");

    btstack_run_loop_execute();

    return 0;
}