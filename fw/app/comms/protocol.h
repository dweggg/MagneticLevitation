#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

/* Host -> device commands. */
#define CMD_READ          0x01
#define CMD_WRITE         0x02
#define CMD_LIST_VARS     0x03
#define CMD_LIST_STREAMS  0x04
#define STATUS_OK         0x00
#define STATUS_ERROR      0x01

/* Device -> host frame: [FRAME_SYNC][type][len][payload][xor checksum]. */
#define FRAME_SYNC        0xA5
#define FRAME_LOG         0x01
#define FRAME_REPLY       0x02
#define FRAME_STREAM      0x03

/* Queue one frame; returns 0 (and sends nothing) if the ring lacks space. */
int protocol_send(uint8_t type, const uint8_t *payload, uint16_t length);
int protocol_log_write(const uint8_t *buf, uint16_t len);
int protocol_log_format(const char *format, ...);
int protocol_bridge_poll(void);

#define LOG(format, ...) do { \
	protocol_log_format(format, ##__VA_ARGS__); \
} while (0)

#endif // PROTOCOL_H
