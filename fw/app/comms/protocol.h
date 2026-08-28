#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

int protocol_log_write(const uint8_t *buf, uint16_t len);
int protocol_log_format(const char *format, ...);

#define LOG(format, ...) do { \
	protocol_log_format(format, ##__VA_ARGS__); \
} while (0)


#endif // PROTOCOL_H