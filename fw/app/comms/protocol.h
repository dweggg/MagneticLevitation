#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

int protocol_log_write(const uint8_t *buf, uint16_t len);

#define LOG(message) do { \
	static const uint8_t log_message[] = message "\r\n"; \
	protocol_log_write(log_message, sizeof(log_message) - 1); \
} while (0)


#endif // PROTOCOL_H