#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

/* Host -> device commands. */
#define PROTOCOL_COMMAND_READ         0x01U
#define PROTOCOL_COMMAND_WRITE        0x02U
#define PROTOCOL_COMMAND_LIST_VARS    0x03U
#define PROTOCOL_COMMAND_LIST_STREAMS 0x04U
#define PROTOCOL_COMMAND_SET_STREAM_SUBSCRIPTION 0x05U
#define PROTOCOL_STATUS_OK            0x00U
#define PROTOCOL_STATUS_ERROR         0x01U

/* Device -> host frame: [PROTOCOL_FRAME_SYNC][type][len][payload][xor checksum]. */
#define PROTOCOL_FRAME_SYNC   0xA5U
#define PROTOCOL_FRAME_LOG    0x01U
#define PROTOCOL_FRAME_REPLY  0x02U
#define PROTOCOL_FRAME_STREAM 0x03U

/* Queue one frame; returns 0 (and sends nothing) if the ring lacks space. */
int protocol_send(uint8_t type, const uint8_t *payload, uint16_t length);
int protocol_log_write(const uint8_t *buf, uint16_t len);
int protocol_log_format(const char *format, ...);
int protocol_bridge_poll(void);
int protocol_stream_variable_enabled(uint16_t id);

#define PROTOCOL_LOG(format, ...) do { \
	protocol_log_format(format, ##__VA_ARGS__); \
} while (0)

#endif // PROTOCOL_H
