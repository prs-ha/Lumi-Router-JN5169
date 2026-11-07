
/**
 * @file app_tts_protocol.h
 * @brief OpenLumi Zigbee TTS protocol
 *
 * Transport: Zigbee -> JN5169 -> UART -> OpenWrt
 *
 * This header defines protocol limits only.
 * No changes to RGB handling.
 */

#ifndef APP_TTS_PROTOCOL_H
#define APP_TTS_PROTOCOL_H

#include <jendefs.h>

/* Protocol version */
#define TTS_PROTOCOL_VERSION 1

/* Maximum UTF-8 text length, in bytes */
#define TTS_MAX_MESSAGE_BYTES 512

/* Target maximum text bytes per fragment */
#define TTS_FRAGMENT_BYTES 64

/* Maximum fragments for a 512-byte message */
#define TTS_MAX_FRAGMENTS 8

/* Supported alert levels */
#define TTS_ALERT_INFO     1
#define TTS_ALERT_WARNING  2
#define TTS_ALERT_URGENT   3

/* Manufacturer-specific Zigbee cluster */
#define TTS_CLUSTER_ID 0xFC10

/* Commands */
#define TTS_CMD_FRAGMENT 0x00
#define TTS_CMD_ACK      0x01
#define TTS_CMD_SHORT    0x02
#define TTS_CMD_DATA_FRAGMENT 0x03

/* Fragment header, excluding Zigbee ZCL header */
#define TTS_FRAGMENT_HEADER_BYTES 7

/* Maximum fragment payload: 7 + 64 = 71 bytes */
#define TTS_FRAGMENT_PAYLOAD_MAX_BYTES 71


/*
 * Fragment payload:
 *
 * version      : 1 byte
 * message_id   : 2 bytes
 * alert_level  : 1 byte
 * fragment_no  : 1 byte (0-based)
 * fragment_cnt : 1 byte
 * data_len     : 1 byte
 * data         : up to 64 bytes
 *
 * Bytes are transported unchanged (UTF-8).
 * The receiver must not play incomplete messages.
 */

#endif /* APP_TTS_PROTOCOL_H */
