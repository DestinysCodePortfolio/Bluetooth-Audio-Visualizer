// main.cpp — BTVisualizer
//
// Core 0: BTstack run loop
// Core 1: LVGL oscilloscope rendering loop
//
// Audio flow:
//   Spotify → BT → A2DP → SBC decode → audio_buf_push() → LVGL oscilloscope
//
// Real SD fallback flow:
//   microSD playlist WAV → sd_wav_fallback_task() → audio_buf_push() → LVGL oscilloscope
//
// Generated fallback flow:
//   fallback_audio_task() → generated PCM samples → audio_buf_push() → LVGL oscilloscope
//
// Buttons active-low, pulled up internally:
//   GP13 = Pause / Play
//   GP14 = Next
//   GP15 = Previous

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/multicore.h"
#include "hardware/gpio.h"

#include "btstack.h"
#include "classic/btstack_sbc.h"
#include "classic/avrcp_controller.h"

#include "audio_buffer.h"
#include "audio_fallback.h"
#include "sd_wav_fallback.h"
#include "spi_display.h"
#include "lv_conf.h"
#include "lvgl_oscilloscope.h"
#include "audio_pwm_output.h"

#include <lvgl.h>

// ------------------------------------------------------------
// Button config
// ------------------------------------------------------------

#define BTN_PAUSE_PIN   13
#define BTN_NEXT_PIN    14
#define BTN_PREV_PIN    15
#define BTN_DEBOUNCE_MS 250

// ------------------------------------------------------------
// Bluetooth stream status
// ------------------------------------------------------------

static volatile bool     bluetooth_streaming = false;
static volatile uint32_t last_bt_audio_ms    = 0;

// ------------------------------------------------------------
// AVRCP controller state
// ------------------------------------------------------------

static uint16_t avrcp_cid       = 0;
static bool     avrcp_connected = false;

// ------------------------------------------------------------
// Playback state
// ------------------------------------------------------------

static bool g_paused = false;

// ------------------------------------------------------------
// SD WAV fallback status
// ------------------------------------------------------------

static bool g_sd_wav_ready = false;

// ------------------------------------------------------------
// Button debounce timestamps
// ------------------------------------------------------------

static uint32_t last_pause_press_ms = 0;
static uint32_t last_next_press_ms  = 0;
static uint32_t last_prev_press_ms  = 0;

// ------------------------------------------------------------
// BTstack registrations
// ------------------------------------------------------------

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

// ------------------------------------------------------------
// Stats
// ------------------------------------------------------------

static volatile uint32_t total_samples_decoded = 0;
static volatile uint32_t total_frames_decoded  = 0;
static volatile int      last_sample_rate      = 0;
static volatile int      last_channels         = 0;

// ------------------------------------------------------------
// Timers
// ------------------------------------------------------------

static btstack_timer_source_t fallback_timer;
static btstack_timer_source_t stats_timer;

// ------------------------------------------------------------
// Core 1 display object
// ------------------------------------------------------------

static ucr::bcoe::cs::cs122::LVGL_Oscilloscope *g_scope = nullptr;

// ------------------------------------------------------------
// Helpers
// ------------------------------------------------------------

static uint32_t now_ms(void) {
    return to_ms_since_boot(get_absolute_time());
}

uint32_t cs122_get_millis(void) {
    return now_ms();
}

// This was the missing function causing:
// error: 'cs122_flush_cb_partial' was not declared in this scope
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

static void stop_all_fallbacks(void) {
    sd_wav_fallback_set_enabled(false);
    fallback_audio_set_enabled(false);
}

static void start_best_fallback(void) {
    bluetooth_streaming = false;

    if (g_paused) {
        return;
    }

    if (g_sd_wav_ready) {
        printf("Fallback: using SD WAV\n");
        sd_wav_fallback_set_enabled(true);
        fallback_audio_set_enabled(false);
    } else {
        printf("Fallback: using generated audio\n");
        sd_wav_fallback_set_enabled(false);
        fallback_audio_set_enabled(true);
    }
}

static void stop_audio_and_resume_fallback(void) {
    bluetooth_streaming = false;
    audio_buf_stop();
    start_best_fallback();
}

// ------------------------------------------------------------
// Button action handlers
// ------------------------------------------------------------

static void handle_pause(void) {
    g_paused = !g_paused;

#if defined(ENABLE_AVRCP_CONTROLLER)
    if (avrcp_connected && bluetooth_streaming) {
        if (g_paused) {
            printf("BTN: BT pause\n");
            avrcp_controller_pause(avrcp_cid);
        } else {
            printf("BTN: BT play\n");
            avrcp_controller_play(avrcp_cid);
        }

        return;
    }
#else
    if (bluetooth_streaming) {
        printf("BTN: BT pause/play requested, but AVRCP controller is not enabled\n");
        return;
    }
#endif

    if (g_paused) {
        printf("BTN: fallback pause\n");
        sd_wav_fallback_set_enabled(false);
        fallback_audio_set_enabled(false);
        audio_buf_stop();
    } else {
        printf("BTN: fallback play\n");
        start_best_fallback();
    }
}

static void handle_next(void) {
#if defined(ENABLE_AVRCP_CONTROLLER)
    if (avrcp_connected && bluetooth_streaming) {
        printf("BTN: BT next\n");
        avrcp_controller_forward(avrcp_cid);
        return;
    }
#else
    if (bluetooth_streaming) {
        printf("BTN: BT next requested, but AVRCP controller is not enabled\n");
        return;
    }
#endif

    if (g_sd_wav_ready) {
        printf("BTN: SD next\n");

        g_paused = false;
        bluetooth_streaming = false;

        fallback_audio_set_enabled(false);

        if (sd_wav_fallback_next()) {
            sd_wav_fallback_set_enabled(true);
            printf("Now playing SD: %s\n", sd_wav_fallback_current_file());
        } else {
            printf("SD next failed, using generated fallback\n");
            fallback_audio_set_enabled(true);
        }

        return;
    }

    printf("BTN: generated fallback has no next track\n");
}

static void handle_prev(void) {
#if defined(ENABLE_AVRCP_CONTROLLER)
    if (avrcp_connected && bluetooth_streaming) {
        printf("BTN: BT prev\n");
        avrcp_controller_backward(avrcp_cid);
        return;
    }
#else
    if (bluetooth_streaming) {
        printf("BTN: BT prev requested, but AVRCP controller is not enabled\n");
        return;
    }
#endif

    if (g_sd_wav_ready) {
        printf("BTN: SD prev\n");

        g_paused = false;
        bluetooth_streaming = false;

        fallback_audio_set_enabled(false);

        if (sd_wav_fallback_prev()) {
            sd_wav_fallback_set_enabled(true);
            printf("Now playing SD: %s\n", sd_wav_fallback_current_file());
        } else {
            printf("SD prev failed, using generated fallback\n");
            fallback_audio_set_enabled(true);
        }

        return;
    }

    printf("BTN: generated fallback has no previous track\n");
}

// ------------------------------------------------------------
// SBC decoded audio handler
// ------------------------------------------------------------

static void sbc_decoded_handler(int16_t *pcm_data,
                                int num_audio_frames,
                                int num_channels,
                                int sample_rate,
                                void *context) {
    (void)context;

    bluetooth_streaming = true;
    last_bt_audio_ms    = now_ms();

    audio_buf_push(
        pcm_data,
        (uint32_t)num_audio_frames,
        AUDIO_SRC_BLUETOOTH
    );

    total_frames_decoded  += 1;
    total_samples_decoded += (uint32_t)num_audio_frames;
    last_sample_rate       = sample_rate;
    last_channels          = num_channels;
}

// ------------------------------------------------------------
// A2DP media handler
// ------------------------------------------------------------

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

// ------------------------------------------------------------
// Fallback + button polling timer
// ------------------------------------------------------------

static void fallback_timer_handler(btstack_timer_source_t *ts) {
    uint32_t t = now_ms();

    // Button polling: active-low with internal pull-up
    if (!gpio_get(BTN_PAUSE_PIN) && (t - last_pause_press_ms > BTN_DEBOUNCE_MS)) {
        last_pause_press_ms = t;
        handle_pause();
    }

    if (!gpio_get(BTN_NEXT_PIN) && (t - last_next_press_ms > BTN_DEBOUNCE_MS)) {
        last_next_press_ms = t;
        handle_next();
    }

    if (!gpio_get(BTN_PREV_PIN) && (t - last_prev_press_ms > BTN_DEBOUNCE_MS)) {
        last_prev_press_ms = t;
        handle_prev();
    }

    // Bluetooth timeout watchdog
    if (bluetooth_streaming && (t - last_bt_audio_ms > 1500)) {
        printf("BT timeout: no audio samples, resuming fallback\n");
        stop_audio_and_resume_fallback();
    }

    // Fallback tasks
    if (!bluetooth_streaming && !g_paused) {
        if (g_sd_wav_ready && sd_wav_fallback_is_enabled()) {
            sd_wav_fallback_task();
        } else if (fallback_audio_is_enabled()) {
            fallback_audio_task();
        }
    }

    btstack_run_loop_set_timer(ts, 20);
    btstack_run_loop_add_timer(ts);
}

// ------------------------------------------------------------
// AVRCP controller packet handler
// ------------------------------------------------------------

#if defined(ENABLE_AVRCP_CONTROLLER)
static void avrcp_controller_packet_handler(uint8_t packet_type,
                                            uint16_t channel,
                                            uint8_t *packet,
                                            uint16_t size) {
    UNUSED(channel);
    UNUSED(size);

    if (packet_type != HCI_EVENT_PACKET) {
        return;
    }

    if (hci_event_packet_get_type(packet) != HCI_EVENT_AVRCP_META) {
        return;
    }

    uint8_t sub = hci_event_avrcp_meta_get_subevent_code(packet);

    switch (sub) {
        case AVRCP_SUBEVENT_CONNECTION_ESTABLISHED: {
            uint8_t status = avrcp_subevent_connection_established_get_status(packet);

            if (status != ERROR_CODE_SUCCESS) {
                printf("AVRCP controller connect failed: 0x%02x\n", status);
                break;
            }

            avrcp_cid       = avrcp_subevent_connection_established_get_avrcp_cid(packet);
            avrcp_connected = true;

            printf("AVRCP controller connected, cid=0x%04x\n", avrcp_cid);
            break;
        }

        case AVRCP_SUBEVENT_CONNECTION_RELEASED:
            avrcp_connected = false;
            avrcp_cid       = 0;
            printf("AVRCP controller disconnected\n");
            break;

        default:
            break;
    }
}
#endif

// ------------------------------------------------------------
// HCI / A2DP event handler
// ------------------------------------------------------------

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

                start_best_fallback();
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

        case HCI_EVENT_CONNECTION_COMPLETE: {
            uint8_t status = hci_event_connection_complete_get_status(packet);
            printf("Connected! status: %d\n", status);

#if defined(ENABLE_AVRCP_CONTROLLER)
            if (status == ERROR_CODE_SUCCESS) {
                bd_addr_t peer_addr;
                hci_event_connection_complete_get_bd_addr(packet, peer_addr);

                uint16_t cid = 0;
                uint8_t err = avrcp_controller_connect(peer_addr, &cid);

                if (err != ERROR_CODE_SUCCESS) {
                    printf("AVRCP controller connect initiation failed: 0x%02x\n", err);
                }
            }
#endif
            break;
        }

        case HCI_EVENT_DISCONNECTION_COMPLETE:
            printf("Disconnected, reason: 0x%02x\n",
                   hci_event_disconnection_complete_get_reason(packet));

            avrcp_connected = false;
            avrcp_cid       = 0;
            g_paused        = false;

            stop_audio_and_resume_fallback();

            gap_discoverable_control(1);
            gap_connectable_control(1);
            break;

        case HCI_EVENT_A2DP_META: {
            uint8_t sub = hci_event_a2dp_meta_get_subevent_code(packet);

            switch (sub) {
                case A2DP_SUBEVENT_STREAM_STARTED:
                    printf("A2DP: stream STARTED\n");

                    bluetooth_streaming = true;
                    last_bt_audio_ms    = now_ms();
                    g_paused            = false;

                    stop_all_fallbacks();
                    break;

                case A2DP_SUBEVENT_STREAM_SUSPENDED:
                    printf("A2DP: stream SUSPENDED\n");
                    stop_audio_and_resume_fallback();
                    break;

                case A2DP_SUBEVENT_STREAM_RELEASED:
                    printf("A2DP: stream RELEASED\n");
                    stop_audio_and_resume_fallback();
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

// ------------------------------------------------------------
// Stats timer
// ------------------------------------------------------------

static void stats_timer_handler(btstack_timer_source_t *ts) {
    static uint32_t last_samples = 0;

    uint32_t now   = total_samples_decoded;
    uint32_t delta = now - last_samples;
    last_samples   = now;

    if (delta > 0) {
        printf("[stats] BT %lu samples/s, %lu Hz, %d ch, total frames=%lu\n",
               (unsigned long)delta,
               (unsigned long)last_sample_rate,
               last_channels,
               (unsigned long)total_frames_decoded);
    } else if (g_paused) {
        printf("[stats] paused\n");
    } else if (g_sd_wav_ready && sd_wav_fallback_is_enabled()) {
        printf("[stats] SD WAV fallback active: %s\n",
               sd_wav_fallback_current_file());
    } else if (fallback_audio_is_enabled()) {
        printf("[stats] generated fallback active\n");
    } else {
        printf("[stats] idle\n");
    }

    btstack_run_loop_set_timer(ts, 1000);
    btstack_run_loop_add_timer(ts);
}

// ------------------------------------------------------------
// Core 1 — LVGL display loop
// ------------------------------------------------------------

void core1_entry(void) {
    sleep_ms(500);

    if (g_scope) {
        g_scope->run();
    }
}

// ------------------------------------------------------------
// main
// ------------------------------------------------------------

int main(void) {
    stdio_init_all();
    sleep_ms(2000);

    printf("\n=== BTVisualizer ===\n");

    // GPIO button setup: active-low, internal pull-up
    const uint btn_pins[] = {
        BTN_PAUSE_PIN,
        BTN_NEXT_PIN,
        BTN_PREV_PIN
    };

    for (int i = 0; i < 3; i++) {
        gpio_init(btn_pins[i]);
        gpio_set_dir(btn_pins[i], GPIO_IN);
        gpio_pull_up(btn_pins[i]);
    }

    printf("Buttons ready: GP%d=pause  GP%d=next  GP%d=prev\n",
           BTN_PAUSE_PIN,
           BTN_NEXT_PIN,
           BTN_PREV_PIN);

    if (cyw43_arch_init()) {
        printf("CYW43 init failed\n");
        return -1;
    }

    audio_buf_init();
    fallback_audio_init();
    audio_pwm_output_init();

    // Starts with song1.wav, then button controls can go through song2.wav...song5.wav
    g_sd_wav_ready = sd_wav_fallback_init_playlist();

    if (g_sd_wav_ready) {
        printf("SD WAV playlist ready: %s\n",
               sd_wav_fallback_current_file());
    } else {
        printf("SD WAV fallback failed, generated fallback will be used\n");
    }

    start_best_fallback();

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

#if defined(ENABLE_AVRCP_CONTROLLER)
    // AVRCP controller lets our physical buttons control phone media.
    avrcp_controller_init();
    avrcp_controller_register_packet_handler(&avrcp_controller_packet_handler);
    printf("AVRCP controller enabled\n");
#else
    printf("AVRCP controller not enabled; buttons still control SD playlist/fallback\n");
#endif

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

    // Stats timer: 1 second
    stats_timer.process = &stats_timer_handler;
    btstack_run_loop_set_timer(&stats_timer, 1000);
    btstack_run_loop_add_timer(&stats_timer);

    // Fallback + buttons timer: 20 ms
    fallback_timer.process = &fallback_timer_handler;
    btstack_run_loop_set_timer(&fallback_timer, 20);
    btstack_run_loop_add_timer(&fallback_timer);

    hci_power_control(HCI_POWER_ON);

    btstack_run_loop_execute();

    return 0;
}