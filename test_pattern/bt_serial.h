// bt_serial.h — Bluetooth Classic SPP serial backend for Pico 2 W.
//
// Include this ONCE from test_pattern.c, inside a
//   #if defined(PICO_BT_SERIAL_ENABLED)
// guard, AFTER including pico/cyw43_arch.h and btstack.h.
//
// Public API (available when PICO_BT_SERIAL_ENABLED is defined):
//   void bt_serial_init(void)       — call after cyw43_arch_init(),
//                                     before multicore_launch_core1()
//   int  bt_serial_getchar(void)    — non-blocking; returns 0-255 or
//                                     PICO_ERROR_TIMEOUT if no data

#pragma once

#include <string.h>
#include "pico/critical_section.h"
#include "pico/async_context.h"
#include "pico/stdlib.h"
#include "pico/stdio/driver.h"   // struct stdio_driver { out_chars, in_chars, ... }
#include "btstack.h"

// ---------------------------------------------------------------------------
// RX ring buffer  (BT bytes received → input_get_event / mygetchar)
// Written by: Core 0 BTstack IRQ (bt_packet_handler, RFCOMM_DATA_PACKET)
// Read by:    Core 0 normal context (bt_serial_getchar)
// critical_section disables IRQs on Core 0 while held, preventing the IRQ
// from re-entering while normal code is reading.
// ---------------------------------------------------------------------------
#define BT_RX_BUF_SIZE 64u
static uint8_t            bt_rx_buf[BT_RX_BUF_SIZE];
static volatile uint8_t   bt_rx_head = 0;
static volatile uint8_t   bt_rx_tail = 0;
static critical_section_t bt_rx_cs;

static void bt_rx_push(uint8_t c)
{
    critical_section_enter_blocking(&bt_rx_cs);
    uint8_t next = (bt_rx_head + 1u) & (BT_RX_BUF_SIZE - 1u);
    if (next != bt_rx_tail) {           // silently drop if full
        bt_rx_buf[bt_rx_head] = c;
        bt_rx_head = next;
    }
    critical_section_exit(&bt_rx_cs);
}

// Non-blocking read. Returns char value (0–255) or PICO_ERROR_TIMEOUT if empty.
int bt_serial_getchar(void)
{
    critical_section_enter_blocking(&bt_rx_cs);
    if (bt_rx_head == bt_rx_tail) {
        critical_section_exit(&bt_rx_cs);
        return PICO_ERROR_TIMEOUT;
    }
    uint8_t c = bt_rx_buf[bt_rx_tail];
    bt_rx_tail = (bt_rx_tail + 1u) & (BT_RX_BUF_SIZE - 1u);
    critical_section_exit(&bt_rx_cs);
    return (int)c;
}

// ---------------------------------------------------------------------------
// TX ring buffer  (printf/putchar output → BT send)
// Written by: Core 0 normal context (bt_stdio_out_chars via printf)
// Read by:    Core 0 BTstack IRQ (RFCOMM_EVENT_CAN_SEND_NOW handler)
//
// BT_TX_BUF_SIZE must be a power of 2 (ring index mask arithmetic).
// 1024 bytes provides enough headroom for the longest single printf burst
// (~565 bytes for print_help()).  The head/tail indices are uint16_t because
// they need to hold values 0..(BT_TX_BUF_SIZE-1); uint8_t wraps at 255.
//
// BT_TX_CHUNK_MAX caps the per-send chunk passed to rfcomm_send().  It is
// kept at 256 to avoid a large stack-allocated array inside the BTstack IRQ
// handler (RFCOMM_EVENT_CAN_SEND_NOW).  The handler re-requests CAN_SEND_NOW
// if more data remains, so large bursts are drained across multiple events.
// ---------------------------------------------------------------------------
#define BT_TX_BUF_SIZE  1024u
#define BT_TX_CHUNK_MAX  256u
static uint8_t            bt_tx_buf[BT_TX_BUF_SIZE];
static volatile uint16_t  bt_tx_head = 0;
static volatile uint16_t  bt_tx_tail = 0;
static critical_section_t bt_tx_cs;

static void bt_tx_push(uint8_t c)
{
    critical_section_enter_blocking(&bt_tx_cs);
    uint16_t next = (bt_tx_head + 1u) & (BT_TX_BUF_SIZE - 1u);
    if (next != bt_tx_tail) {
        bt_tx_buf[bt_tx_head] = c;
        bt_tx_head = next;
    }
    critical_section_exit(&bt_tx_cs);
}

// Pop one byte from TX ring. Must be called with bt_tx_cs already held.
static int bt_tx_pop_locked(void)
{
    if (bt_tx_head == bt_tx_tail) return -1;
    uint8_t c = bt_tx_buf[bt_tx_tail];
    bt_tx_tail = (bt_tx_tail + 1u) & (BT_TX_BUF_SIZE - 1u);
    return (int)c;
}

// ---------------------------------------------------------------------------
// BTstack state
// ---------------------------------------------------------------------------
static uint16_t  bt_rfcomm_cid = 0;
static uint16_t  bt_rfcomm_mtu = 0;
static uint8_t   bt_spp_service_buffer[150];
static btstack_packet_callback_registration_t bt_hci_cb;

// ---------------------------------------------------------------------------
// Packet handler — called from BTstack IRQ context (Core 0 background task)
// ---------------------------------------------------------------------------
static void bt_packet_handler(uint8_t ptype, uint16_t ch, uint8_t *packet, uint16_t size)
{
    UNUSED(ch);
    bd_addr_t addr;

    switch (ptype) {
    case HCI_EVENT_PACKET:
        switch (hci_event_packet_get_type(packet)) {

        case HCI_EVENT_PIN_CODE_REQUEST:
            // Legacy pairing fallback: respond with PIN "0000"
            hci_event_pin_code_request_get_bd_addr(packet, addr);
            gap_pin_code_response(addr, "0000");
            break;

        case HCI_EVENT_USER_CONFIRMATION_REQUEST:
            // SSP Just Works: auto-accept — no user interaction required
            break;

        case RFCOMM_EVENT_INCOMING_CONNECTION:
            // Accept the incoming SPP connection
            bt_rfcomm_cid = rfcomm_event_incoming_connection_get_rfcomm_cid(packet);
            rfcomm_accept_connection(bt_rfcomm_cid);
            break;

        case RFCOMM_EVENT_CHANNEL_OPENED:
            if (rfcomm_event_channel_opened_get_status(packet) == 0) {
                bt_rfcomm_cid = rfcomm_event_channel_opened_get_rfcomm_cid(packet);
                bt_rfcomm_mtu = rfcomm_event_channel_opened_get_max_frame_size(packet);
            } else {
                bt_rfcomm_cid = 0;
                bt_rfcomm_mtu = 0;
            }
            break;

        case RFCOMM_EVENT_CAN_SEND_NOW: {
            // Drain TX ring buffer: send up to min(bt_rfcomm_mtu, BT_TX_CHUNK_MAX)
            // bytes per event.  If more data remains in the ring, re-request
            // CAN_SEND_NOW so the handler fires again to drain the next chunk.
            uint8_t  chunk[BT_TX_CHUNK_MAX];
            uint16_t maxsend = (bt_rfcomm_mtu > 0 && bt_rfcomm_mtu < BT_TX_CHUNK_MAX)
                               ? bt_rfcomm_mtu : BT_TX_CHUNK_MAX;
            uint16_t n = 0;
            critical_section_enter_blocking(&bt_tx_cs);
            while (n < maxsend) {
                int c = bt_tx_pop_locked();
                if (c < 0) break;
                chunk[n++] = (uint8_t)c;
            }
            bool more = (bt_tx_head != bt_tx_tail);
            critical_section_exit(&bt_tx_cs);
            if (n > 0) rfcomm_send(bt_rfcomm_cid, chunk, n);
            if (more) rfcomm_request_can_send_now_event(bt_rfcomm_cid);
            break;
        }

        case RFCOMM_EVENT_CHANNEL_CLOSED:
            bt_rfcomm_cid = 0;
            bt_rfcomm_mtu = 0;
            break;

        default:
            break;
        }
        break;

    case RFCOMM_DATA_PACKET:
        // Push each received byte into the RX ring for input_get_event / mygetchar
        for (uint16_t i = 0; i < size; i++) bt_rx_push(packet[i]);
        break;

    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// stdio driver — transparent printf/putchar redirection over BT
//
// Registered with stdio_set_driver_enabled().  out_chars pushes bytes to the
// TX ring buffer then triggers a BTstack CAN_SEND_NOW event (under the CYW43
// async context lock) so the packet handler drains and sends the buffer.
// in_chars reads from the RX ring (same data as bt_serial_getchar).
// ---------------------------------------------------------------------------
static void bt_stdio_out_chars(const char *buf, int len)
{
    for (int i = 0; i < len; i++) bt_tx_push((uint8_t)buf[i]);

    if (bt_rfcomm_cid) {
        // rfcomm_request_can_send_now_event() must be called with the CYW43
        // async context lock held when called from outside BTstack callbacks.
        async_context_t *ctx = cyw43_arch_async_context();
        async_context_acquire_lock_blocking(ctx);
        rfcomm_request_can_send_now_event(bt_rfcomm_cid);
        async_context_release_lock(ctx);
    }
}

static int bt_stdio_in_chars(char *buf, int len)
{
    int n = 0;
    while (n < len) {
        int c = bt_serial_getchar();
        if (c < 0) break;
        buf[n++] = (char)c;
    }
    return n ? n : PICO_ERROR_NO_DATA;
}

static stdio_driver_t bt_stdio_driver = {
    .out_chars = bt_stdio_out_chars,
    .in_chars  = bt_stdio_in_chars,
};

// ---------------------------------------------------------------------------
// bt_serial_init()
//
// Call AFTER cyw43_arch_init() and BEFORE multicore_launch_core1().
// Sets up the BTstack SPP (RFCOMM) service, makes the device discoverable
// as "PicoLCD", and registers the stdio driver so printf also goes to BT.
// ---------------------------------------------------------------------------
static void bt_serial_init(void)
{
    critical_section_init(&bt_rx_cs);
    critical_section_init(&bt_tx_cs);

    // Register a general HCI event handler (handles connection state logging)
    bt_hci_cb.callback = &bt_packet_handler;
    hci_add_event_handler(&bt_hci_cb);

    // Initialise protocol layers: L2CAP → RFCOMM → SDP
    l2cap_init();
    rfcomm_init();
    rfcomm_register_service(bt_packet_handler, 1 /*RFCOMM channel*/, 0xffff /*MTU*/);

    sdp_init();
    memset(bt_spp_service_buffer, 0, sizeof(bt_spp_service_buffer));
    spp_create_sdp_record(bt_spp_service_buffer,
                          sdp_create_service_record_handle(),
                          1 /*RFCOMM channel*/, "PicoLCD");
    sdp_register_service(bt_spp_service_buffer);

    // Discoverable and connectable; Just Works pairing (no PIN, no confirmation UI needed).
    // gap_discoverable_control(1) enables inquiry scan  — device appears in BT searches.
    // gap_connectable_control(1)  enables page scan     — device accepts incoming connections.
    // Both must be set; gap_discoverable_control alone only sets scan_enable = 0x01
    // (inquiry-only), which makes the device visible but unreachable.
    gap_discoverable_control(1);
    gap_connectable_control(1);
    gap_ssp_set_io_capability(SSP_IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
    gap_set_local_name("PicoLCD");

    // Power on — BTstack IRQs start firing via threadsafe_background context
    hci_power_control(HCI_POWER_ON);

    // Add BT as a second stdio backend alongside USB CDC
    stdio_set_driver_enabled(&bt_stdio_driver, true);
}
