#ifndef BTSTACK_CONFIG_H
#define BTSTACK_CONFIG_H

// ===== REQUIRED FOR A2DP CLASSIC =====
#ifndef ENABLE_CLASSIC
#define ENABLE_CLASSIC
#endif

#define ENABLE_A2DP_SINK
#define ENABLE_AVDTP_ACCEPTOR
#define ENABLE_AVRCP_TARGET
#define ENABLE_SBC_DECODER
// Required configurations for the CYW43 Bluetooth driver
#define HCI_OUTGOING_PRE_BUFFER_SIZE 4
#define HCI_ACL_CHUNK_SIZE_ALIGNMENT 4
// ===== IMPORTANT: disable BLE completely =====
// (BLE is what is breaking your build right now)

// ===== MEMORY / STACK =====
#define NVM_NUM_LINK_KEYS 16
#define HCI_ACL_PAYLOAD_SIZE 1024

// ===== DEBUG =====
#define ENABLE_LOG_INFO
#define ENABLE_LOG_ERROR
#define ENABLE_PRINTF_HEXDUMP

#endif