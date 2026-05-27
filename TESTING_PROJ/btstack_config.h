#ifndef BTSTACK_CONFIG_H
#define BTSTACK_CONFIG_H

#ifndef ENABLE_CLASSIC
#define ENABLE_CLASSIC
#endif
#ifndef ENABLE_BLE
#define ENABLE_BLE
#endif
#ifndef ENABLE_LE_PERIPHERAL
#define ENABLE_LE_PERIPHERAL
#endif
#ifndef ENABLE_LE_CENTRAL
#define ENABLE_LE_CENTRAL
#endif
#ifndef ENABLE_LOG_INFO
#define ENABLE_LOG_INFO
#endif
#ifndef ENABLE_LOG_ERROR
#define ENABLE_LOG_ERROR
#endif
#ifndef ENABLE_PRINTF_HEXDUMP
#define ENABLE_PRINTF_HEXDUMP
#endif

#define NVM_NUM_LINK_KEYS                        4
#define HCI_OUTGOING_PRE_BUFFER_SIZE             4
#define HCI_ACL_PAYLOAD_SIZE                     (1691 + 4)
#define HCI_ACL_CHUNK_SIZE_ALIGNMENT             4

#define MAX_NR_L2CAP_CHANNELS                    4
#define MAX_NR_L2CAP_SERVICES                    4
#define MAX_NR_RFCOMM_MULTIPLEXERS               0
#define MAX_NR_RFCOMM_SERVICES                   0
#define MAX_NR_RFCOMM_CHANNELS                   0
#define MAX_NR_BTSTACK_LINK_KEY_DB_MEMORY_ENTRIES 2
#define MAX_NR_BTM_DEDICATED_INQUIRY_RESULTS      4
#define MAX_NR_WHITELIST_ENTRIES                 0
#define MAX_NR_SM_LOOKUP_ENTRIES                 3
#define MAX_NR_SERVICE_RECORD_ITEMS              4
#define MAX_NR_AVDTP_STREAM_ENDPOINTS            1
#define MAX_NR_AVDTP_CONNECTIONS                 1
#define MAX_NR_AVRCP_CONNECTIONS                 1
#define MAX_ATT_DB_SIZE                          512
#define MAX_NR_LE_DEVICE_DB_ENTRIES              4
#define NVM_NUM_DEVICE_DB_ENTRIES         4
#endif
