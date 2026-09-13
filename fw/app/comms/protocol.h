#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>
#include "parameters.h"

#define PARAM_CMD_READ   0x01
#define PARAM_CMD_WRITE  0x02
#define PARAM_CMD_LIST   0x03
#define PARAM_REPLY      0x80
#define PARAM_STATUS_OK    0x00
#define PARAM_STATUS_ERROR 0x01

int protocol_log_write(const uint8_t *buf, uint16_t len);
int protocol_log_format(const char *format, ...);
int protocol_bridge_poll(void);

#define LOG(format, ...) do { \
	protocol_log_format(format, ##__VA_ARGS__); \
} while (0)

#endif // PROTOCOL_H