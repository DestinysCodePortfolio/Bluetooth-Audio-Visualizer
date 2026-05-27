#include <stdio.h>
#include <string.h>
#include <math.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "btstack.h"

static uint8_t audio_level = 0;
static uint8_t led_control = 0;
static uint16_t connection_handle = HCI_CON_HANDLE_INVALID;

// Simple advertisement name
static const char adv_name[] = "PicoAudioViz";

static void hci_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size) {
    UNUSED(channel);
    UNUSED(size);
    
    if (packet_type != HCI_EVENT_PACKET) return;
    
    uint8_t event_type = hci_event_packet_get_type(packet);
    
    switch(event_type) {
        case BTSTACK_EVENT_STATE:
            if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING) {
                printf("Bluetooth powered on - advertising\n");
                
                // Set GAP mode to connectable + discoverable
                gap_set_local_name((const uint8_t *)adv_name);
                gap_discoverable_control(1);
                gap_connectable_control(1);
                
                printf("Advertising as '%s'\n", adv_name);
            }
            break;
            
        case HCI_EVENT_LE_META:
            {
                uint8_t subevent = hci_event_le_meta_get_subevent_code(packet);
                if (subevent == HCI_SUBEVENT_LE_CONNECTION_COMPLETE) {
                    connection_handle = hci_subevent_le_connection_complete_get_connection_handle(packet);
                    printf("Connected! Handle: %u\n", connection_handle);
                }
            }
            break;
            
        case HCI_EVENT_DISCONNECTION_COMPLETE:
            connection_handle = HCI_CON_HANDLE_INVALID;
            printf("Disconnected\n");
            break;
            
        default:
            break;
    }
}

int main(void) {
    stdio_init_all();
    printf("\n===== Audio Visualizer =====\n");

    if (cyw43_arch_init()) {
        printf("CYW43 init failed\n");
        return -1;
    }

    printf("CYW43 initialized\n");

    // Initialize BTstack
    btstack_memory_init();
    hci_init(hci_transport_h4_instance(hci_uart_transport_instance()), NULL);
    hci_set_chipset(cyw43_bt_chipset_instance());

    // Setup protocols
    l2cap_init();
    le_device_db_init();
    gatt_client_init();
    gatt_server_init();
    sm_init();

    // Register HCI packet handler  
    hci_event_callback_registration_t callback;
    callback.callback = &hci_packet_handler;
    hci_add_event_handler(&callback);

    // Power on
   hci_power_control(HCI_POWER_ON);
    printf("Entering run loop\n");
    
    // Heartbeat thread before runloop blocks
    for (int i = 0; i < 5; i++) {
        printf("Startup heartbeat %d\n", i);
        sleep_ms(1000);
    }
    
    btstack_run_loop_execute();
    return 0;
}
