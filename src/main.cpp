// main.cpp — BTVisualizer
//
// Core 0: BTstack run loop
// Core 1: LVGL oscilloscope rendering loop
//
// Bluetooth:
//   Phone / Spotify -> A2DP -> SBC decode -> audio_buf_push() -> LVGL / PWM
//
// Fallback:
//   SD WAV playlist -> sd_wav_fallback_task() -> audio_buf_push() -> LVGL / PWM
//
// Buttons are local SD fallback controls only:
//   GP13 = Pause / Play SD fallback
//   GP14 = Next SD WAV file
//   GP15 = Previous SD WAV file
//
// Important:
//   This file does NOT enable AVRCP controller because that broke Spotify/A2DP.
//   Bluetooth audio is controlled from the phone.

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/multicore.h"
#include "hardware/gpio.h"

#include "btstack.h"
#include "classic/btstack_sbc.h"

#include "audio_buffer.h"
#include "audio_fallback.h"
#include "audio_pwm_output.h"
#include "sd_wav_fallback.h"
#include "spi_display.h"
#include "lv_conf.h"
#include "lvgl_oscilloscope.h"

#include <lvgl.h>

// ------------------------------------------------------------
// Button config
// ------------------------------------------------------------

#define BTN_PAUSE_PIN   13
#define BTN_NEXT_PIN    14
#define BTN_PREV_PIN    15
#define BTN_DEBOUNCE_MS 250
#define TEST_SD_ONLY 1
// ------------------------------------------------------------
// Bluetooth stream status
// ------------------------------------------------------------

static volatile bool     bluetooth_streaming = false;
static volatile uint32_t last_bt_audio_ms    = 0;

// ------------------------------------------------------------
// Playback / fallback state
// ------------------------------------------------------------

static bool g_paused       = false;
static bool g_sd_wav_ready = false;

static uint32_t last_pause_press_ms = 0;
static uint32_t last_next_press_ms  = 0;
static uint32_t last_prev_press_ms  = 0;

// ------------------------------------------------------------
// BTstack registrations / buffers
// ------------------------------------------------------------

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
static avdtp_stream_endpoint_t *local_stream_endpoint = NULL;
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

static ucr::bcoe::cs::cs122::LVGL_Oscilloscope *g_scope = NULL;

// ------------------------------------------------------------
// Time helpers
// ------------------------------------------------------------

static uint32_t now_ms(void) {
    return to_ms_since_boot(get_absolute_time());
}

uint32_t cs122_get_millis(void) {
    return now_ms();
}

// ------------------------------------------------------------
// LVGL flush callback
// ------------------------------------------------------------

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

// ------------------------------------------------------------
// Fallback helpers
// ------------------------------------------------------------

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
        printf("Fallback: using SD WAV: %s\n", sd_wav_fallback_current_file());
        sd_wav_fallback_set_enabled(true);
        fallback_audio_set_enabled(false);
    } else {
        printf("Fallback: SD not ready, generated fallback muted\n");
        sd_wav_fallback_set_enabled(false);
        fallback_audio_set_enabled(false);
    }
}

static void stop_audio_and_resume_fallback(void) {
    bluetooth_streaming = false;
    audio_buf_stop();
    start_best_fallback();
}

// ------------------------------------------------------------
// Local button controls: SD fallback only
// ------------------------------------------------------------

static void handle_pause_button(void) {
    if (bluetooth_streaming) {
        printf("BTN: BT active, use phone for pause/play\n");
        return;
    }

    if (!g_sd_wav_ready) {
        printf("BTN: SD not ready, pause ignored\n");
        return;
    }

    g_paused = !g_paused;

    if (g_paused) {
        printf("BTN: SD pause\n");
        sd_wav_fallback_set_enabled(false);
        fallback_audio_set_enabled(false);
        audio_buf_stop();
    } else {
        printf("BTN: SD play/resume: %s\n", sd_wav_fallback_current_file());
        sd_wav_fallback_set_enabled(true);
        fallback_audio_set_enabled(false);
    }
}

static void handle_next_button(void) {
    if (bluetooth_streaming) {
        printf("BTN: BT active, next ignored\n");
        return;
    }

    if (!g_sd_wav_ready) {
        printf("BTN: SD not ready, no next track\n");
        return;
    }

    printf("BTN: SD next\n");

    g_paused = false;
    fallback_audio_set_enabled(false);

    if (sd_wav_fallback_next()) {
        printf("Now playing SD: %s\n", sd_wav_fallback_current_file());
        sd_wav_fallback_set_enabled(true);
    } else {
        printf("SD next failed; muting fallback\n");
        sd_wav_fallback_set_enabled(false);
        fallback_audio_set_enabled(false);
    }
}

static void handle_prev_button(void) {
    if (bluetooth_streaming) {
        printf("BTN: BT active, previous ignored\n");
        return;
    }

    if (!g_sd_wav_ready) {
        printf("BTN: SD not ready, no previous track\n");
        return;
    }

    printf("BTN: SD previous\n");

    g_paused = false;
    fallback_audio_set_enabled(false);

    if (sd_wav_fallback_prev()) {
        printf("Now playing SD: %s\n", sd_wav_fallback_current_file());
        sd_wav_fallback_set_enabled(true);
    } else {
        printf("SD previous failed; muting fallback\n");
        sd_wav_fallback_set_enabled(false);
        fallback_audio_set_enabled(false);
    }
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

    // Raw button debug: not pressed = 1, pressed = 0.
    static uint32_t last_button_debug_ms = 0;
    if (t - last_button_debug_ms > 1000) {
        last_button_debug_ms = t;
        printf("BTN RAW: GP13=%d GP14=%d GP15=%d\n",
               gpio_get(BTN_PAUSE_PIN),
               gpio_get(BTN_NEXT_PIN),
               gpio_get(BTN_PREV_PIN));
    }

    // Button polling: active-low with internal pull-up.
    if (!gpio_get(BTN_PAUSE_PIN) && (t - last_pause_press_ms > BTN_DEBOUNCE_MS)) {
        last_pause_press_ms = t;
        handle_pause_button();
    }

    if (!gpio_get(BTN_NEXT_PIN) && (t - last_next_press_ms > BTN_DEBOUNCE_MS)) {
        last_next_press_ms = t;
        handle_next_button();
    }

    if (!gpio_get(BTN_PREV_PIN) && (t - last_prev_press_ms > BTN_DEBOUNCE_MS)) {
        last_prev_press_ms = t;
        handle_prev_button();
    }

    // Bluetooth timeout watchdog.
    if (bluetooth_streaming && (t - last_bt_audio_ms > 1500)) {
        printf("BT timeout: no audio samples, resuming fallback\n");
        stop_audio_and_resume_fallback();
    }

    // Fallback tasks.
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

        case HCI_EVENT_CONNECTION_COMPLETE:
            printf("Connected! status: %d\n",
                   hci_event_connection_complete_get_status(packet));
            break;

        case HCI_EVENT_DISCONNECTION_COMPLETE:
            printf("Disconnected, reason: 0x%02x\n",
                   hci_event_disconnection_complete_get_reason(packet));
            g_paused = false;
            stop_audio_and_resume_fallback();
            gap_discoverable_control(1);
            gap_connectable_control(1);
            break;

        case HCI_EVENT_A2DP_META: {
            uint8_t subevent = hci_event_a2dp_meta_get_subevent_code(packet);

            switch (subevent) {
                case A2DP_SUBEVENT_STREAM_STARTED:
                    printf("A2DP: stream STARTED\n");
                    bluetooth_streaming = true;
                    last_bt_audio_ms = now_ms();
                    g_paused = false;
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
                    printf("A2DP event subcode: 0x%02x\n", subevent);
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
        printf("[stats] SD WAV active: %s\n", sd_wav_fallback_current_file());
    } else if (fallback_audio_is_enabled()) {
        printf("[stats] generated fallback active\n");
    } else {
        printf("[stats] idle\n");
    }

    btstack_run_loop_set_timer(ts, 1000);
    btstack_run_loop_add_timer(ts);
}

// ------------------------------------------------------------
// Core 1 display loop
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

    // GPIO button setup: active-low, internal pull-up.
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

    printf("Buttons ready: GP%d=pause GP%d=next GP%d=prev\n",
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

    printf("Trying SD WAV playlist...\n");
    printf("MAIN DEBUG: about to init SD playlist\n");
    
    g_sd_wav_ready = sd_wav_fallback_init_playlist();
   
    printf("MAIN DEBUG: sd_wav_fallback_init_playlist returned %d\n", g_sd_wav_ready);
    
    if (g_sd_wav_ready) {
        printf("SD WAV playlist ready: %s\n", sd_wav_fallback_current_file());
        sd_wav_fallback_set_enabled(true);
        fallback_audio_set_enabled(false);
    } else {
        printf("ERROR: SD WAV fallback failed. Generated fallback muted.\n");
        sd_wav_fallback_set_enabled(false);
        fallback_audio_set_enabled(false);
    }

    start_best_fallback();

    // Display + oscilloscope.
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

    // BTstack setup.
    l2cap_init();
    sdp_init();
    sm_init();

    gap_ssp_set_io_capability(SSP_IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
    gap_ssp_set_auto_accept(1);
    gap_set_bondable_mode(1);

    // A2DP sink.
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

    // AVRCP target only. Do not add AVRCP controller.
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

    // SBC decoder.
    btstack_sbc_decoder_init(
        &sbc_decoder_state,
        SBC_MODE_STANDARD,
        sbc_decoded_handler,
        NULL
    );

    printf("SBC decoder initialized\n");

    // GAP.
    gap_set_local_name("BTVisualizer");
    gap_set_class_of_device(0x240414);

    hci_event_cb.callback = &packet_handler;
    hci_add_event_handler(&hci_event_cb);

    // Stats timer.
    stats_timer.process = &stats_timer_handler;
    btstack_run_loop_set_timer(&stats_timer, 1000);
    btstack_run_loop_add_timer(&stats_timer);

    // Fallback + button polling timer.
    fallback_timer.process = &fallback_timer_handler;
    btstack_run_loop_set_timer(&fallback_timer, 20);
    btstack_run_loop_add_timer(&fallback_timer);
    
    #if TEST_SD_ONLY
    printf("TEST_SD_ONLY mode: Bluetooth disabled.\n");
    while (true) {
        if (!g_paused) {
            if (g_sd_wav_ready && sd_wav_fallback_is_enabled()) {
                sd_wav_fallback_task();
            } else if (fallback_audio_is_enabled()) {
                fallback_audio_task();
            }
        }

        sleep_ms(5);
    }
    #else
    hci_power_control(HCI_POWER_ON);
    btstack_run_loop_execute();
    #endif

    return 0;
}
