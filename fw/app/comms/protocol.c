#include "protocol.h"
#include "usb_cdc.h"
#include <stdarg.h>
#include <string.h>

#define PROTOCOL_MAX_FRAME_BYTES 256

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

static int protocol_send_frame(const uint8_t *payload, uint8_t length)
{
	uint8_t frame[PROTOCOL_MAX_FRAME_BYTES];
	if (length + 1 > sizeof(frame)) {
		return 0;
	}

	frame[0] = PARAM_REPLY;
	memcpy(&frame[1], payload, length);
	return usb_cdc_tx_send(frame, (int)length + 1);
}

static int protocol_send_status_response(uint8_t status)
{
	return protocol_send_frame(&status, 1);
}

static int protocol_send_object_response(uint16_t id, const uint8_t *data, uint8_t length)
{
	uint8_t reply[4 + PROTOCOL_MAX_FRAME_BYTES];
	uint8_t reply_len = 0;

	reply[reply_len++] = (uint8_t)(id & 0xFF);
	reply[reply_len++] = (uint8_t)((id >> 8) & 0xFF);
	reply[reply_len++] = length;
	memcpy(&reply[reply_len], data, length);
	reply_len += length;
	return protocol_send_frame(reply, reply_len);
}

static int protocol_send_list_response(void)
{
	size_t count = 0;
	const parameter_descriptor_t *map = parameters_map(&count);
	uint8_t frame[1 + 32 * 32];
	uint8_t index = 0;

	frame[index++] = (uint8_t)count;
	for (size_t i = 0; i < count; ++i) {
		const parameter_descriptor_t *desc = &map[i];
		const char *name = desc->name != NULL ? desc->name : "";
		size_t name_length = strlen(name);
		if (name_length > 31U) {
			name_length = 31U;
		}

		frame[index++] = (uint8_t)(desc->id & 0xFF);
		frame[index++] = (uint8_t)((desc->id >> 8) & 0xFF);
		frame[index++] = desc->direction;
		frame[index++] = desc->format;
		frame[index++] = desc->size;
		frame[index++] = (uint8_t)name_length;
		memcpy(&frame[index], name, name_length);
		index += (uint8_t)name_length;
	}
	return protocol_send_frame(frame, index);
}

static int protocol_read_value(uint16_t id, uint8_t *buffer, uint8_t buffer_size)
{
	const parameter_descriptor_t *desc = parameters_find(id);
	if (desc == NULL || buffer == NULL || buffer_size < desc->size) {
		return -1;
	}
	if ((desc->direction & PARAM_DIR_TX) == 0) {
		return -2;
	}

	if (parameters_fetch(id, buffer, buffer_size) < 0) {
		return -3;
	}
	return (int)desc->size;
}

static int protocol_write_value(uint16_t id, const uint8_t *buffer, uint8_t length)
{
	const parameter_descriptor_t *desc = parameters_find(id);
	if (desc == NULL || buffer == NULL || length != desc->size) {
		return -1;
	}
	if ((desc->direction & PARAM_DIR_RX) == 0) {
		return -2;
	}

	return parameters_publish(id, buffer);
}

int protocol_log_write(const uint8_t *buf, uint16_t len)
{
	return usb_cdc_tx_send(buf, len);
}

int protocol_log_format(const char *format, ...)
{
	char log_message[96];
	va_list arguments;
	uint16_t length = 0;
	static const char log_prefix[] = "[LOG] ";

	memcpy(log_message, log_prefix, sizeof(log_prefix) - 1);
	length = (uint16_t)(sizeof(log_prefix) - 1);

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

int protocol_bridge_poll(void)
{
	uint8_t frame[PROTOCOL_MAX_FRAME_BYTES];
	int available = usb_cdc_rx_available();
	if (available <= 0) {
		return 0;
	}

	int read_count = usb_cdc_rx_read(frame, available > (int)sizeof(frame) ? (int)sizeof(frame) : available);
	if (read_count <= 0) {
		return 0;
	}

	for (int pos = 0; pos < read_count; ) {
		uint8_t command = frame[pos++];
		if (command == PARAM_CMD_LIST) {
			protocol_send_list_response();
			continue;
		}
		if (pos + 1 >= read_count) {
			break;
		}

		uint16_t id = (uint16_t)frame[pos] | ((uint16_t)frame[pos + 1] << 8);
		pos += 2;

		if (command == PARAM_CMD_READ) {
			uint8_t value_buffer[4] = {0};
			int result = protocol_read_value(id, value_buffer, sizeof(value_buffer));
			if (result >= 0) {
				protocol_send_object_response(id, value_buffer, (uint8_t)result);
			} else {
				protocol_send_status_response(PARAM_STATUS_ERROR);
			}
			continue;
		}

		if (command == PARAM_CMD_WRITE) {
			if (pos >= read_count) {
				protocol_send_status_response(PARAM_STATUS_ERROR);
				break;
			}
			uint8_t payload_length = frame[pos++];
			if (payload_length == 0 || pos + payload_length > read_count) {
				protocol_send_status_response(PARAM_STATUS_ERROR);
				break;
			}
			int write_result = protocol_write_value(id, &frame[pos], payload_length);
			if (write_result == 0) {
				protocol_send_status_response(PARAM_STATUS_OK);
			} else {
				protocol_send_status_response(PARAM_STATUS_ERROR);
			}
			pos += payload_length;
		}
	}

	return read_count;
}