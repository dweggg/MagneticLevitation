#include "protocol.h"
#include "usb_cdc.h"
#include <stdarg.h>

static uint16_t append_text(char *buffer, uint16_t position, uint16_t size,
                            const char *text)
{
	while (*text != '\0' && position < size - 1) {
		buffer[position++] = *text++;
	}

	return position;
}

static uint16_t append_unsigned(char *buffer, uint16_t position, uint16_t size,
                                unsigned int value)
{
	char digits[10];
	uint16_t digit_count = 0;

	do {
		digits[digit_count++] = (char)('0' + value % 10U);
		value /= 10U;
	} while (value != 0U);

	while (digit_count != 0 && position < size - 1) {
		buffer[position++] = digits[--digit_count];
	}

	return position;
}

static uint16_t append_unsigned_long(char *buffer, uint16_t position, uint16_t size,
                                    unsigned long value)
{
	char digits[20];
	uint16_t digit_count = 0;

	do {
		digits[digit_count++] = (char)('0' + (value % 10UL));
		value /= 10UL;
	} while (value != 0UL);

	while (digit_count != 0 && position < size - 1) {
		buffer[position++] = digits[--digit_count];
	}

	return position;
}

static uint16_t append_hex(char *buffer, uint16_t position, uint16_t size,
                          unsigned long value)
{
	char digits[16];
	uint16_t digit_count = 0;

	do {
		unsigned int nibble = value & 0xFUL;
		digits[digit_count++] = (char)(nibble < 10U ? ('0' + nibble) : ('a' + (nibble - 10U)));
		value >>= 4U;
	} while (value != 0UL);

	while (digit_count != 0 && position < size - 1) {
		buffer[position++] = digits[--digit_count];
	}

	return position;
}

int protocol_log_write(const uint8_t *buf, uint16_t len)
{
	return usb_cdc_tx_send(buf, len);
}

int protocol_log_format(const char *format, ...)
{
	char log_message[64];
	va_list arguments;
	uint16_t length = 0;

	va_start(arguments, format);
	while (*format != '\0' && length < sizeof(log_message) - 3) {
		if (*format != '%') {
			log_message[length++] = *format++;
			continue;
		}

		format++;
		int is_long = 0;
		if (*format == 'l') {
			is_long = 1;
			format++;
		}

		switch (*format++) {
		case 'u':
			if (is_long) {
				length = append_unsigned_long(log_message, length, sizeof(log_message),
				                            va_arg(arguments, unsigned long));
			} else {
				length = append_unsigned(log_message, length, sizeof(log_message),
				                       va_arg(arguments, unsigned int));
			}
			break;
		case 'x':
			if (is_long) {
				length = append_hex(log_message, length, sizeof(log_message),
				                   va_arg(arguments, unsigned long));
			} else {
				length = append_hex(log_message, length, sizeof(log_message),
				                   va_arg(arguments, unsigned int));
			}
			break;
		case 'X':
			if (is_long) {
				length = append_hex(log_message, length, sizeof(log_message),
				                   va_arg(arguments, unsigned long));
			} else {
				length = append_hex(log_message, length, sizeof(log_message),
				                   va_arg(arguments, unsigned int));
			}
			break;
		case 's':
			length = append_text(log_message, length, sizeof(log_message),
			                     va_arg(arguments, const char *));
			break;
		case '%':
			log_message[length++] = '%';
			break;
		default:
			log_message[length++] = '?';
			break;
		}
	}
	va_end(arguments);

	log_message[length++] = '\r';
	log_message[length++] = '\n';

	return protocol_log_write((const uint8_t *)log_message, (uint16_t)length);
}