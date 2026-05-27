#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "btstack.h"

static btstack_packet_callback_registration_t hci_event_cb;
static uint8_t sdp_a2dp_sink_buffer[220];
static uint8_t sdp_avrcp_buffer[220];
static uint8_t media_sbc_codec_capabilities[] = {
    (AVDTP_SBC_44100 << 4) | AVDTP_SBC_STEREO,
    (AVDTP_SBC_BLOCK_LENGTH_16 << 4) | (AVDTP_SBC_SUBBANDS_8 << 2) | AVDTP_SBC_ALLOCATION_METHOD_LOUDNESS,
    2, 53
};
static uint8_t media_sbc_codec_configuration[4];
static avdtp_stream_endpoint_t * local_stream_endpoint;

static void packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size) {
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
            printf("Simple pairing complete, status: %d\n", hci_event_simple_pairing_complete_get_status(packet));
            break;
        case SM_EVENT_PAIRING_STARTED: {
            bd_addr_t address;
            sm_event_pairing_started_get_address(packet, address);
            printf("SM pairing started for %02x:%02x:%02x:%02x:%02x:%02x\n",
                   address[0], address[1], address[2], address[3], address[4], address[5]);
            break;
        }
        case SM_EVENT_PAIRING_COMPLETE: {
            bd_addr_t address;
            sm_event_pairing_complete_get_address(packet, address);
            printf("SM pairing complete status=%u reason=0x%02x for %02x:%02x:%02x:%02x:%02x:%02x\n",
                   sm_event_pairing_complete_get_status(packet),
                   sm_event_pairing_complete_get_reason(packet),
                   address[0], address[1], address[2], address[3], address[4], address[5]);
            break;
        }
        case HCI_EVENT_CONNECTION_REQUEST:
            printf("Connection request incoming\n");
            break;
        case HCI_EVENT_CONNECTION_COMPLETE:
            printf("Connected! status: %d\n", hci_event_connection_complete_get_status(packet));
            break;
        case HCI_EVENT_DISCONNECTION_COMPLETE:
            printf("Disconnected, reason: 0x%02x\n", hci_event_disconnection_complete_get_reason(packet));
            gap_discoverable_control(1);
            break;
        case HCI_EVENT_A2DP_META:
            printf("A2DP event subcode: 0x%02x\n", hci_event_a2dp_meta_get_subevent_code(packet));
            break;
        default:
            break;
    }
}

int main(void) {
    stdio_init_all();
    sleep_ms(2000);
    printf("\n=== BTVisualizer ===\n");

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

    a2dp_sink_init();
    a2dp_sink_register_packet_handler(&packet_handler);

    // Register SBC stream endpoint - this is what was missing
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
    a2dp_sink_create_sdp_record(sdp_a2dp_sink_buffer, sdp_create_service_record_handle(), 0, NULL, NULL);
    sdp_register_service(sdp_a2dp_sink_buffer);

    avrcp_init();
    avrcp_target_init();
    memset(sdp_avrcp_buffer, 0, sizeof(sdp_avrcp_buffer));
    avrcp_target_create_sdp_record(sdp_avrcp_buffer, sdp_create_service_record_handle(), 0, NULL, NULL);
    sdp_register_service(sdp_avrcp_buffer);

    gap_set_local_name("BTVisualizer");
    gap_set_class_of_device(0x200428);  // Audio - Hands-free/Headphones combo
    gap_set_default_link_policy_settings(LM_LINK_POLICY_ENABLE_SNIFF_MODE | LM_LINK_POLICY_ENABLE_ROLE_SWITCH);

    hci_event_cb.callback = &packet_handler;
    hci_add_event_handler(&hci_event_cb);

    hci_power_control(HCI_POWER_ON);
    while(1) { printf("heartbeat\n"); sleep_ms(2000); btstack_run_loop_execute(); }
    return 0;
}
