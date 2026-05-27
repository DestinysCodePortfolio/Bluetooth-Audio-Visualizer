#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "btstack.h"
#include "hardware/pwm.h"
#include "hardware/dma.h"
#include <string.h>

#define AUDIO_PWM_PIN   18
#define SAMPLE_RATE     44100

static btstack_packet_callback_registration_t hci_event_callback_registration;
static uint8_t  sdp_a2dp_sink_service_buffer[150];
static uint8_t  sdp_avrcp_target_service_buffer[150];

// SBC decoder state
static btstack_sbc_decoder_state_t sbc_decoder_state;

// PWM DMA audio output
static int dma_chan;
static uint pwm_slice;

static void audio_init(void) {
    gpio_set_function(AUDIO_PWM_PIN, GPIO_FUNC_PWM);
    pwm_slice = pwm_gpio_to_slice_num(AUDIO_PWM_PIN);
    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_clkdiv(&cfg, 3.0f);
    pwm_config_set_wrap(&cfg, 255);
    pwm_init(pwm_slice, &cfg, true);

    dma_chan = dma_claim_unused_channel(true);
    dma_channel_config dc = dma_channel_get_default_config(dma_chan);
    channel_config_set_transfer_data_size(&dc, DMA_SIZE_8);
    channel_config_set_dreq(&dc, pwm_get_dreq(pwm_slice));
    dma_channel_configure(dma_chan, &dc,
        &pwm_hw->slice[pwm_slice].cc,
        NULL, 0, false);
}

// Called by SBC decoder with decoded PCM samples
static void sbc_decoded_callback(int16_t *pcm_data, int num_audio_frames,
                                  int num_channels, int sample_rate, void *context) {
    (void)num_channels; (void)sample_rate; (void)context;

    static uint8_t pwm_buf[512];
    int n = num_audio_frames > 512 ? 512 : num_audio_frames;
    for (int i = 0; i < n; i++) {
        pwm_buf[i] = (uint8_t)((pcm_data[i] >> 8) + 128);
    }
    dma_channel_set_read_addr(dma_chan, pwm_buf, false);
    dma_channel_set_trans_count(dma_chan, n, true);

    // TODO: also forward pcm_data to FPGA over SPI here
}

// Raw media packet handler — feeds SBC frames to the decoder
static void a2dp_media_handler(uint8_t local_seid, uint8_t *packet, uint16_t size) {
    // Skip RTP header (12 bytes) to get to SBC payload
    if (size < 13) return;
    uint8_t *sbc_data = packet + 13;
    uint16_t sbc_size = size - 13;
    btstack_sbc_decoder_process_data(&sbc_decoder_state, 0, sbc_data, sbc_size);
}

static void hci_packet_handler(uint8_t type, uint16_t channel,
                                uint8_t *packet, uint16_t size) {
    if (type != HCI_EVENT_PACKET) return;
    switch (hci_event_packet_get_type(packet)) {
        case BTSTACK_EVENT_STATE:
            if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING) {
                printf("BT ready. Discoverable as 'BTVisualizer'\n");
                gap_discoverable_control(1);
                gap_connectable_control(1);
            }
            break;
        case HCI_EVENT_A2DP_META:
            switch (hci_event_a2dp_meta_get_subevent_code(packet)) {
                case A2DP_SUBEVENT_STREAM_STARTED:
                    printf("A2DP stream started\n");
                    break;
                case A2DP_SUBEVENT_STREAM_SUSPENDED:
                    printf("A2DP stream paused\n");
                    break;
            }
            break;
        default:
            break;
    }
}

void a2dp_sink_setup(void) {
    audio_init();

    // Init SBC decoder
    btstack_sbc_decoder_init(&sbc_decoder_state, SBC_MODE_STANDARD,
                              sbc_decoded_callback, NULL);

    hci_event_callback_registration.callback = &hci_packet_handler;
    hci_add_event_handler(&hci_event_callback_registration);

    gap_set_local_name("BTVisualizer");

    a2dp_sink_init();
    a2dp_sink_register_packet_handler(&hci_packet_handler);
    a2dp_sink_register_media_handler(&a2dp_media_handler);  // now correct signature

    memset(sdp_a2dp_sink_service_buffer, 0, sizeof(sdp_a2dp_sink_service_buffer));
    a2dp_sink_create_sdp_record(sdp_a2dp_sink_service_buffer,
                                sdp_create_service_record_handle(), 1, NULL, NULL);
    sdp_register_service(sdp_a2dp_sink_service_buffer);

    avrcp_target_init();
    memset(sdp_avrcp_target_service_buffer, 0, sizeof(sdp_avrcp_target_service_buffer));
    avrcp_target_create_sdp_record(sdp_avrcp_target_service_buffer,
                                   sdp_create_service_record_handle(), 0, NULL, NULL);
    sdp_register_service(sdp_avrcp_target_service_buffer);

    sdp_init();
}