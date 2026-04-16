// btstack_config.h — BTstack configuration for PicoLCD test pattern generator.
// Based on the Pico W examples config from pico-examples/pico_w/bt/config/btstack_config.h.
// ENABLE_CLASSIC (and ENABLE_BLE if BLE is linked) are injected as -D flags by
// the pico_btstack_classic / pico_btstack_ble CMake targets; do not redefine them here.

#ifndef _PICO_BTSTACK_BTSTACK_CONFIG_H
#define _PICO_BTSTACK_BTSTACK_CONFIG_H

// BTstack logging features
#define ENABLE_LOG_INFO
#define ENABLE_LOG_ERROR
#define ENABLE_PRINTF_HEXDUMP
#define ENABLE_SCO_OVER_HCI

#ifdef ENABLE_BLE
#define ENABLE_GATT_CLIENT_PAIRING
#define ENABLE_L2CAP_LE_CREDIT_BASED_FLOW_CONTROL_MODE
#define ENABLE_LE_CENTRAL
#define ENABLE_LE_DATA_LENGTH_EXTENSION
#define ENABLE_LE_PERIPHERAL
#define ENABLE_LE_PRIVACY_ADDRESS_RESOLUTION
#define ENABLE_LE_SECURE_CONNECTIONS
#endif

#ifdef ENABLE_CLASSIC
#define ENABLE_L2CAP_ENHANCED_RETRANSMISSION_MODE
#define ENABLE_GOEP_L2CAP
#endif

#if defined(ENABLE_CLASSIC) && defined(ENABLE_BLE)
#define ENABLE_CROSS_TRANSPORT_KEY_DERIVATION
#endif

// Buffer and resource sizing
#define HCI_OUTGOING_PRE_BUFFER_SIZE        4
#define HCI_ACL_PAYLOAD_SIZE                (1691 + 4)
#define HCI_ACL_CHUNK_SIZE_ALIGNMENT        4
#define MAX_NR_AVDTP_CONNECTIONS            1
#define MAX_NR_AVDTP_STREAM_ENDPOINTS       1
#define MAX_NR_AVRCP_CONNECTIONS            2
#define MAX_NR_BNEP_CHANNELS                1
#define MAX_NR_BNEP_SERVICES                1
#define MAX_NR_BTSTACK_LINK_KEY_DB_MEMORY_ENTRIES  2
#define MAX_NR_GATT_CLIENTS                 1
#define MAX_NR_HCI_CONNECTIONS              2
#define MAX_NR_HID_HOST_CONNECTIONS         1
#define MAX_NR_HIDS_CLIENTS                 1
#define MAX_NR_HFP_CONNECTIONS              1
#define MAX_NR_L2CAP_CHANNELS               4
#define MAX_NR_L2CAP_SERVICES               3
#define MAX_NR_RFCOMM_CHANNELS              1
#define MAX_NR_RFCOMM_MULTIPLEXERS          1
#define MAX_NR_RFCOMM_SERVICES              1
#define MAX_NR_SERVICE_RECORD_ITEMS         4
#define MAX_NR_SM_LOOKUP_ENTRIES            3
#define MAX_NR_WHITELIST_ENTRIES            16
#define MAX_NR_LE_DEVICE_DB_ENTRIES         16

// Limit ACL/SCO buffers to avoid CYW43 shared-bus overrun
#define MAX_NR_CONTROLLER_ACL_BUFFERS       3
#define MAX_NR_CONTROLLER_SCO_PACKETS       3

// HCI controller-to-host flow control to avoid CYW43 shared-bus overrun
#define ENABLE_HCI_CONTROLLER_TO_HOST_FLOW_CONTROL
#define HCI_HOST_ACL_PACKET_LEN             1024
#define HCI_HOST_ACL_PACKET_NUM             3
#define HCI_HOST_SCO_PACKET_LEN             120
#define HCI_HOST_SCO_PACKET_NUM             3

// Link Key DB and LE Device DB using TLV on Flash
#define NVM_NUM_DEVICE_DB_ENTRIES           16
#define NVM_NUM_LINK_KEYS                   16

// Fixed-size ATT DB (no malloc in BTstack)
#define MAX_ATT_DB_SIZE                     512

// BTstack HAL
#define HAVE_EMBEDDED_TIME_MS
#define HAVE_ASSERT                         // maps btstack_assert onto Pico SDK assert()

#define HCI_RESET_RESEND_TIMEOUT_MS         1000

#define ENABLE_SOFTWARE_AES128
#define ENABLE_MICRO_ECC_FOR_LE_SECURE_CONNECTIONS

#define HAVE_BTSTACK_STDIN

#endif // _PICO_BTSTACK_BTSTACK_CONFIG_H
