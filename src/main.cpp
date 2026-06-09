// main.cpp — BTVisualizer
//
// Core 0: BTstack run loop
// Core 1: LVGL oscilloscope rendering loop
//
// Audio flow:
//   Spotify → BT → A2DP → SBC decode → audio_buf_push() → LVGL oscilloscope
//
// Fallback flow:
//   fallback_audio_task() → generated PCM samples → audio_buf_push() → LVGL oscilloscope
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/multicore.h"

#include "btstack.h"
#include "classic/btstack_sbc.h"

#include "audio_buffer.h"
#include "audio_fallback.h"
#include "spi_display.h"
#include "lv_conf.h"
#include "lvgl_oscilloscope.h"

#include <lvgl.h>

// Bluetooth stream status
static volatile bool bluetooth_streaming = false;

// BTstack registrations
static btstack_packet_callback_registration_t hci_event_cb;
static uint8_t sdp_a2dp_sink_buffer[220];
static uint8_t sdp_avrcp_buffer[220];

// SBC codec capabilities
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

// SBC decoder state
static btstack_sbc_decoder_state_t sbc_decoder_state;

// Stats
static volatile uint32_t total_samples_decoded = 0;
static volatile uint32_t total_frames_decoded  = 0;
static volatile int      last_sample_rate      = 0;
static volatile int      last_channels         = 0;

// Fallback timer
static btstack_timer_source_t fallback_timer;

// LVGL timing
uint32_t cs122_get_millis(void) {
    return to_ms_since_boot(get_absolute_time());
}

// LVGL display flush callback
void cs122_flush_cb_partial(lv_display_t *disp,
                            const lv_area_t *area,
                            uint8_t *px_buf) {
    ucr::bcoe::SPIDisplay *spi_display =
        reinterpret_cast<ucr::bcoe::SPIDisplay *>(lv_display_get_user_data(disp));

    spi_display->drawBitmap(
        2 * area->x1,
        area->y1,
        2 * area->x2 + 1,
        area->y2,
        px_buf
    );

    lv_display_flush_ready(disp);
}

// SBC decode callback
// This is where decoded Bluetooth PCM audio arrives.
static void sbc_decoded_handler(int16_t *pcm_data,
                                int num_audio_frames,
                                int num_channels,
                                int sample_rate,
                                void *context) {
    (void)context;

    bluetooth_streaming = true;

    // Feed Bluetooth samples into visualizer pipeline
    audio_buf_push(
        pcm_data,
        (uint32_t)num_audio_frames,
        AUDIO_SRC_BLUETOOTH
    );

    // Update stats
    total_frames_decoded  += 1;
    total_samples_decoded += (uint32_t)num_audio_frames;
    last_sample_rate       = sample_rate;
    last_channels          = num_channels;
}

// A2DP media handler
// Strips RTP header and feeds SBC data to decoder.
static void a2dp_media_handler(uint8_t seid,
                               uint8_t *packet,
                               uint16_t size) {
    (void)seid;

    // 12-byte RTP header + 1-byte SBC payload header = 13 bytes
    if (size < 13) {
        return;
    }

    btstack_sbc_decoder_process_data(
        &sbc_decoder_state,
        0,
        packet + 13,
        size - 13
    );
}

// Fallback timer callback
// This keeps fallback audio running while BTstack owns Core 0.
static void fallback_timer_handler(btstack_timer_source_t *ts) {
    if (!bluetooth_streaming && fallback_audio_is_enabled()) {
        fallback_audio_task();
    }

    btstack_run_loop_set_timer(ts, 20);   // about 50 updates/sec
    btstack_run_loop_add_timer(ts);
}

// HCI / A2DP / AVRCP event handler
static void packet_handler(uint8_t packet_type,
                           uint16_t channel,
                           uint8_t *packet,
                           uint16_t size) {
    UNUSED(channel);
    UNUSED(size);

    if (packet_type != HCI_EVENT_PACKET) {
        return;
    }

    uint8_t event = hci_event_packet_get_type(packet);

    switch (event) {
        case BTSTACK_EVENT_STATE:
            if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING) {
                printf("BT ready - discoverable as BTVisualizer\n");

                gap_discoverable_control(1);
                gap_connectable_control(1);

                // Start fallback by default until Bluetooth stream starts
                bluetooth_streaming = false;
                fallback_audio_set_enabled(true);
            }
            break;

        case HCI_EVENT_PIN_CODE_REQUEST: {
            printf("PIN code request - using 0000\n");

            bd_addr_t bd_addr;
            hci_event_pin_code_request_get_bd_addr(packet, bd_addr);
            gap_pin_code_response(bd_addr, "0000");
            break;
        }

        case HCI_EVENT_USER_CONFIRMATION_REQUEST: {
            printf("SSP confirm - auto accept\n");

            bd_addr_t bd_addr;
            hci_event_user_confirmation_request_get_bd_addr(packet, bd_addr);
            gap_ssp_confirmation_response(bd_addr);
            break;
        }

        case HCI_EVENT_SIMPLE_PAIRING_COMPLETE:
            printf("Simple pairing complete, status: %d\n",
                   hci_event_simple_pairing_complete_get_status(packet));
            break;

        case HCI_EVENT_CONNECTION_COMPLETE:
            printf("Connected! status: %d\n",
                   hci_event_connection_complete_get_status(packet));
            break;

        case HCI_EVENT_DISCONNECTION_COMPLETE:
            printf("Disconnected, reason: 0x%02x\n",
                   hci_event_disconnection_complete_get_reason(packet));

            bluetooth_streaming = false;
            audio_buf_stop();

            // Turn fallback back on after disconnect
            fallback_audio_set_enabled(true);

            gap_discoverable_control(1);
            gap_connectable_control(1);
            break;

        case HCI_EVENT_A2DP_META: {
            uint8_t sub = hci_event_a2dp_meta_get_subevent_code(packet);

            switch (sub) {
                case A2DP_SUBEVENT_STREAM_STARTED:
                    printf("A2DP: stream STARTED\n");

                    bluetooth_streaming = true;

                    // Bluetooth is working, so stop fallback
                    fallback_audio_set_enabled(false);
                    break;

                case A2DP_SUBEVENT_STREAM_SUSPENDED:
                    printf("A2DP: stream SUSPENDED\n");

                    bluetooth_streaming = false;
                    audio_buf_stop();

                    // Resume fallback when music is paused
                    fallback_audio_set_enabled(true);
                    break;

                case A2DP_SUBEVENT_STREAM_RELEASED:
                    printf("A2DP: stream RELEASED\n");

                    bluetooth_streaming = false;
                    audio_buf_stop();

                    // Resume fallback when stream ends
                    fallback_audio_set_enabled(true);
                    break;

                default:
                    printf("A2DP event subcode: 0x%02x\n", sub);
                    break;
            }

            break;
        }

        default:
            break;
    }
}

// Stats timer
static btstack_timer_source_t stats_timer;

static void stats_timer_handler(btstack_timer_source_t *ts) {
    static uint32_t last_samples = 0;

    uint32_t now = total_samples_decoded;
    uint32_t delta = now - last_samples;
    last_samples = now;

    if (delta > 0) {
        printf("[stats] %lu samples/s, %lu Hz, %d ch, total frames=%lu\n",
               (unsigned long)delta,
               (unsigned long)last_sample_rate,
               last_channels,
               (unsigned long)total_frames_decoded);
    } else if (fallback_audio_is_enabled()) {
        printf("[stats] fallback audio active\n");
    }

    btstack_run_loop_set_timer(ts, 1000);
    btstack_run_loop_add_timer(ts);
}

// Core 1 — LVGL display loop
static ucr::bcoe::cs::cs122::LVGL_Oscilloscope *g_scope = nullptr;

void core1_entry(void) {
    sleep_ms(500);
    g_scope->run();
}

// main — Core 0
int main(void) {
    stdio_init_all();
    sleep_ms(3000);

    printf("\n=== BTVisualizer ===\n");

    if (cyw43_arch_init()) {
        printf("CYW43 init failed\n");
        return -1;
    }

    // Display + oscilloscope
    static ucr::bcoe::SPIDisplay spi_display(480, 272, 10000000, 20);

    spi_display.begin();
    spi_display.clear();

    static ucr::bcoe::cs::cs122::LVGL_Oscilloscope scope(
        &spi_display,
        cs122_flush_cb_partial,
        cs122_get_millis
    );

    g_scope = &scope;

    // Initialize fallback audio before starting BT
    audio_buf_init();
    fallback_audio_init();
    fallback_audio_set_enabled(true);



    // Start LCD rendering on Core 1
    multicore_launch_core1(core1_entry);

    // BTstack setup
    l2cap_init();
    sdp_init();
    sm_init();

    gap_ssp_set_io_capability(SSP_IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
    gap_ssp_set_auto_accept(1);
    gap_set_bondable_mode(1);

    // A2DP sink
    a2dp_sink_init();
    a2dp_sink_register_packet_handler(&packet_handler);
    a2dp_sink_register_media_handler(&a2dp_media_handler);

    local_stream_endpoint = a2dp_sink_create_stream_endpoint(
        AVDTP_AUDIO,
        AVDTP_CODEC_SBC,
        media_sbc_codec_capabilities,
        sizeof(media_sbc_codec_capabilities),
        media_sbc_codec_configuration,
        sizeof(media_sbc_codec_configuration)
    );

    if (!local_stream_endpoint) {
        printf("ERROR: could not create stream endpoint\n");
        return -1;
    }

    printf("Stream endpoint registered\n");

    memset(sdp_a2dp_sink_buffer, 0, sizeof(sdp_a2dp_sink_buffer));

    a2dp_sink_create_sdp_record(
        sdp_a2dp_sink_buffer,
        sdp_create_service_record_handle(),
        0,
        NULL,
        NULL
    );

    sdp_register_service(sdp_a2dp_sink_buffer);

    // AVRCP target
    avrcp_init();
    avrcp_target_init();

    memset(sdp_avrcp_buffer, 0, sizeof(sdp_avrcp_buffer));

    avrcp_target_create_sdp_record(
        sdp_avrcp_buffer,
        sdp_create_service_record_handle(),
        0,
        NULL,
        NULL
    );

    sdp_register_service(sdp_avrcp_buffer);

    // SBC decoder
    btstack_sbc_decoder_init(
        &sbc_decoder_state,
        SBC_MODE_STANDARD,
        sbc_decoded_handler,
        NULL
    );

    printf("SBC decoder initialized\n");

    // GAP
    gap_set_local_name("BTVisualizer");
    gap_set_class_of_device(0x240414);

    hci_event_cb.callback = &packet_handler;
    hci_add_event_handler(&hci_event_cb);

    // Stats timer
    stats_timer.process = &stats_timer_handler;
    btstack_run_loop_set_timer(&stats_timer, 1000);
    btstack_run_loop_add_timer(&stats_timer);

    // Fallback timer
    fallback_timer.process = &fallback_timer_handler;
    btstack_run_loop_set_timer(&fallback_timer, 20);
    btstack_run_loop_add_timer(&fallback_timer);

    // Start Bluetooth
    hci_power_control(HCI_POWER_ON);

    // BTstack owns Core 0 from here
    btstack_run_loop_execute();

    return 0;
}