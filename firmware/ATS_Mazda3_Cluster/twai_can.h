// =============================================================================
//  twai_can.h - the ESP32's built-in CAN controller (TWAI) behind the same
//  calls the sketch makes on the Seeed MCP2515 library, so both buses can share
//  the send, receive and `#stat` code.
//
//  Used for HS-CAN on the Autosport Labs ESP32-CAN-X2 (CAN1, connector X1).
//  Return codes and error flags reuse the Seeed library's CAN_* and
//  MCP_EFLG_* values.
// =============================================================================
#pragma once

#include <driver/twai.h>
#include <mcp2515_can.h>

class TwaiCan {
 public:
  TwaiCan(int txPin, int rxPin) : txPin_(txPin), rxPin_(rxPin) {}

  // speed is one of the Seeed library's CAN_xxxKBPS values; clock is unused
  // (the TWAI runs from the ESP32's own clock) and is only there so the call
  // matches mcp2515_can::begin().
  byte begin(uint32_t speed, byte clock = 0) {
    (void)clock;
    twai_timing_config_t timing;
    switch (speed) {
      case CAN_125KBPS: timing = TWAI_TIMING_CONFIG_125KBITS(); break;
      case CAN_250KBPS: timing = TWAI_TIMING_CONFIG_250KBITS(); break;
      case CAN_500KBPS: timing = TWAI_TIMING_CONFIG_500KBITS(); break;
      case CAN_1000KBPS: timing = TWAI_TIMING_CONFIG_1MBITS(); break;
      default: return CAN_FAILINIT;
    }
    twai_general_config_t general =
        TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)txPin_, (gpio_num_t)rxPin_, TWAI_MODE_NORMAL);
    general.tx_queue_len = 16;
    general.rx_queue_len = 32;
    twai_filter_config_t filter = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    if (twai_driver_install(&general, &timing, &filter) != ESP_OK) return CAN_FAILINIT;
    if (twai_start() != ESP_OK) return CAN_FAILINIT;
    return CAN_OK;
  }

  // Queues the frame without waiting. A full queue means nothing is getting
  // out (no other node acknowledging), the same as the MCP2515's three
  // transmit buffers all being busy.
  byte sendMsgBuf(unsigned long id, byte ext, byte len, const byte *buf) {
    twai_message_t msg = {};
    msg.identifier = id;
    msg.extd = ext ? 1 : 0;
    msg.data_length_code = len > 8 ? 8 : len;
    memcpy(msg.data, buf, msg.data_length_code);
    return twai_transmit(&msg, 0) == ESP_OK ? CAN_OK : CAN_GETTXBFTIMEOUT;
  }

  // Also restarts the controller after a bus-off, which the MCP2515 does on
  // its own but the TWAI leaves to the application.
  byte checkReceive() {
    twai_status_info_t st;
    if (twai_get_status_info(&st) == ESP_OK) {
      if (st.state == TWAI_STATE_BUS_OFF) twai_initiate_recovery();
      else if (st.state == TWAI_STATE_STOPPED) twai_start();
      if (st.msgs_to_rx > 0) return CAN_MSGAVAIL;
    }
    return CAN_NOMSG;
  }

  byte readMsgBufID(unsigned long *id, byte *len, byte *buf) {
    twai_message_t msg;
    if (twai_receive(&msg, 0) != ESP_OK) return CAN_NOMSG;
    *id = msg.identifier;
    *len = msg.data_length_code > 8 ? 8 : msg.data_length_code;
    memcpy(buf, msg.data, *len);
    return CAN_OK;
  }

  // Fills *eflg with MCP2515-style error flags worked out from the TWAI's
  // error counters, so `#stat` reads the same on both controllers.
  byte checkError(uint8_t *eflg = NULL) {
    uint8_t flags = 0;
    twai_status_info_t st;
    if (twai_get_status_info(&st) == ESP_OK) {
      if (st.state == TWAI_STATE_BUS_OFF || st.state == TWAI_STATE_RECOVERING) flags |= MCP_EFLG_TXBO;
      if (st.tx_error_counter >= 128) flags |= MCP_EFLG_TXEP;
      if (st.rx_error_counter >= 128) flags |= MCP_EFLG_RXEP;
      if (st.tx_error_counter >= 96) flags |= MCP_EFLG_TXWAR;
      if (st.rx_error_counter >= 96) flags |= MCP_EFLG_RXWAR;
      if (flags & (MCP_EFLG_TXWAR | MCP_EFLG_RXWAR)) flags |= MCP_EFLG_EWARN;
    }
    if (eflg) *eflg = flags;
    return flags ? CAN_CTRLERROR : CAN_OK;
  }

 private:
  int txPin_;
  int rxPin_;
};
