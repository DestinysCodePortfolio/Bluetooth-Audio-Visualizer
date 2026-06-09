#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "btstack.h"
#include "classic/btstack_sbc.h"

// ---- BTstack registrations ----
static btstack_packet_callback_registration_t hci_event_cb;
static uint8_t sdp_a2dp_sink_buffer[220];
static uint8_t sdp_avrcp_buffer[220];

// ---- SBC codec caps offered to the source ----
static uint8_t media_sbc_codec_capabilities[] = {
    (AVDTP_SBC_44100 << 4) | AVDTP_SBC_STEREO,
    (AVDTP_SBC_BLOCK_LENGTH_16 << 4) | (AVDTP_SBC_SUBBANDS_8 << 2) | AVDTP_SBC_ALLOCATION_METHOD_LOUDNESS,
    2, 53
};
static uint8_t media_sbc_codec_configuration[4];
static avdtp_stream_endpoint_t *local_stream_endpoint;

// ---- SBC decoder state ----
static btstack_sbc_decoder_state_t sbc_decoder_state;

// ---- Stats (printed periodically) ----
static volatile uint32_t total_samples_decoded = 0;
static volatile uint32_t total_frames_decoded  = 0;
static volatile int      last_sample_rate      = 0;
static volatile int      last_channels         = 0;

// Called by SBC decoder for each chunk of PCM it produces.
// pcm_data is interleaved if stereo (L,R,L,R,...).
static void sbc_decoded_handler(int16_t *pcm_data,
                                int num_audio_frames,
                                int num_channels,
                                int sample_rate,
                                void *context) {
    (void)context;
    (void)pcm_data;  // we'll consume this in Step 2 (SPI to FPGA)
    total_frames_decoded  += 1;
    total_samples_decoded += (uint32_t)num_audio_frames;
    last_sample_rate       = sample_rate;
    last_channels          = num_channels;
}

// Receives the raw AVDTP media payload (RTP + SBC frames).
// We strip the RTP header and feed the rest to the decoder.
static void a2dp_media_handler(uint8_t seid, uint8_t *packet, uint16_t size) {
    (void)seid;
    // 12-byte RTP header + 1-byte SBC payload header = skip 13 bytes
    if (size < 13) return;
    btstack_sbc_decoder_process_data(&sbc_decoder_state, 0,
                                     packet + 13, size - 13);
}

// ---- HCI / A2DP / AVRCP events ----
static void packet_handler(uint8_t packet_type, uint16_t channel,
                            uint8_t *packet, uint16_t size) {
    UNUSED(channel); UNUSED(size);
    if (packet_type != HCI_EVENT_PACKET) return;
    uint8_t event = hci_event_packet_get_type(packet);

    switch (event) {
        case BTSTACK_EVENT_STATE:
            if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING) {
                printf("BT ready - discoverable as BTVisualizer\n");
                gap_discoverable_control(1);
                gap_connectable_control(1);
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
            gap_discoverable_control(1);
            break;
        case HCI_EVENT_A2DP_META: {
            uint8_t sub = hci_event_a2dp_meta_get_subevent_code(packet);
            switch (sub) {
                case A2DP_SUBEVENT_STREAM_STARTED:
                    printf("A2DP: stream STARTED\n");
                    break;
                case A2DP_SUBEVENT_STREAM_SUSPENDED:
                    printf("A2DP: stream SUSPENDED\n");
                    break;
                case A2DP_SUBEVENT_STREAM_RELEASED:
                    printf("A2DP: stream RELEASED\n");
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

// Periodic stats heartbeat (1 s)
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
    }
    btstack_run_loop_set_timer(ts, 1000);
    btstack_run_loop_add_timer(ts);
}

int main(void) {
    stdio_init_all();
    sleep_ms(3000);
    printf("\n=== BTVisualizer (Step 1: SBC decode) ===\n");

    if (cyw43_arch_init()) {
        printf("CYW43 init failed\n");
        return -1;
    }

    l2cap_init();
    sdp_init();
    sm_init();

    gap_ssp_set_io_capability(SSP_IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
    gap_ssp_set_auto_accept(1);
    gap_set_bondable_mode(1);

    // ---- A2DP sink ----
    a2dp_sink_init();
    a2dp_sink_register_packet_handler(&packet_handler);
    a2dp_sink_register_media_handler(&a2dp_media_handler);

    local_stream_endpoint = a2dp_sink_create_stream_endpoint(
        AVDTP_AUDIO, AVDTP_CODEC_SBC,
        media_sbc_codec_capabilities, sizeof(media_sbc_codec_capabilities),
        media_sbc_codec_configuration, sizeof(media_sbc_codec_configuration));
    if (!local_stream_endpoint) {
        printf("ERROR: could not create stream endpoint\n");
        return -1;
    }
    printf("Stream endpoint registered\n");

    memset(sdp_a2dp_sink_buffer, 0, sizeof(sdp_a2dp_sink_buffer));
    a2dp_sink_create_sdp_record(sdp_a2dp_sink_buffer,
                                sdp_create_service_record_handle(), 0, NULL, NULL);
    sdp_register_service(sdp_a2dp_sink_buffer);

    // ---- AVRCP target ----
    avrcp_init();
    avrcp_target_init();
    memset(sdp_avrcp_buffer, 0, sizeof(sdp_avrcp_buffer));
    avrcp_target_create_sdp_record(sdp_avrcp_buffer,
                                   sdp_create_service_record_handle(), 0, NULL, NULL);
    sdp_register_service(sdp_avrcp_buffer);

    // ---- SBC decoder ----
    btstack_sbc_decoder_init(&sbc_decoder_state, SBC_MODE_STANDARD,
                             sbc_decoded_handler, NULL);
    printf("SBC decoder initialized\n");

    // ---- GAP basics ----
    gap_set_local_name("BTVisualizer");
    gap_set_class_of_device(0x240414);

    hci_event_cb.callback = &packet_handler;
    hci_add_event_handler(&hci_event_cb);

    // ---- Stats timer ----
    stats_timer.process = &stats_timer_handler;
    btstack_run_loop_set_timer(&stats_timer, 1000);
    btstack_run_loop_add_timer(&stats_timer);

    hci_power_control(HCI_POWER_ON);
    btstack_run_loop_execute();
    return 0;
}
