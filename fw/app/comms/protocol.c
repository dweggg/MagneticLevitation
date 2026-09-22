#include "protocol.h"
#include "usb_cdc.h"
#include "vars.h"
#include <stdarg.h>
#include <string.h>

#define PROTOCOL_RX_BYTES 128U
#define LIST_MIN_FREE_BYTES 64U

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

/*
 * Device -> host framing (every byte the device sends is inside a frame):
 *   [0xA5][type][len][payload (len bytes)][xor of all preceding bytes]
 * Frame types: FRAME_LOG (ASCII), FRAME_REPLY (command reply),
 * FRAME_STREAM ([stream id][u32 tick LE][sample bytes]).
 *
 * Host -> device commands are unframed and short:
 *   READ [id16], WRITE [id16][len][data], LIST_VARS, LIST_STREAMS.
 * Replies are [cmd][status][body...]; list replies carry one entry per frame.
 */

#define PROTOCOL_TX_CONTROL_RESERVE 128U

int protocol_send(uint8_t type, const uint8_t *payload, uint16_t length)
{
	int required = (int)length + 4;
	int reserve = (type == FRAME_STREAM) ? PROTOCOL_TX_CONTROL_RESERVE : 0;
	if (length > 255U || usb_cdc_tx_free() < required + reserve) {
		return 0;   /* all-or-nothing: never emit a partial frame */
	}

	uint8_t header[3] = { FRAME_SYNC, type, (uint8_t)length };
	uint8_t checksum = 0;
	for (uint8_t i = 0; i < 3U; ++i) {
		checksum ^= header[i];
	}
	for (uint16_t i = 0; i < length; ++i) {
		checksum ^= payload[i];
	}

	usb_cdc_tx_send(header, 3);
	usb_cdc_tx_send(payload, length);
	usb_cdc_tx_send(&checksum, 1);
	/* Prime a newly-idle bulk IN endpoint immediately; the 5 kHz CDC task
	 * remains as a recovery path for host/USB races. */
	usb_cdc_tx_kick();
	return 1;
}

static int send_reply(uint8_t cmd, uint8_t status, const uint8_t *body, uint8_t body_len)
{
	uint8_t reply[2 + VAR_MAX_BODY];
	reply[0] = cmd;
	reply[1] = status;
	if (body_len != 0U) {
		memcpy(&reply[2], body, body_len);
	}
	return protocol_send(FRAME_REPLY, reply, (uint16_t)(2U + body_len));
}

static void put_u16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8U); }

/* Progressive LIST: entries are sent as ring space allows, so a long
 * list never overflows the transmit ring or fights the telemetry for it. */
static uint8_t list_cmd;    /* 0 = idle */
static uint8_t list_index;

static int send_list_entry(void)
{
	uint8_t body[8 + VAR_NAME_MAX + 4];
	uint8_t n = 0;
	const char *name;
	size_t total;

	body[n++] = list_index;
	if (list_cmd == CMD_LIST_VARS) {
		const var_t *v = var_at(list_index);
		total = var_count();
		name = v->name;
		body[n++] = (uint8_t)total;
		put_u16(&body[n], v->id); n += 2;
		body[n++] = v->direction;
		body[n++] = v->format;
		body[n++] = v->size;
		body[n++] = v->stream;
		body[n++] = v->offset;
	} else {
		const stream_t *s = stream_at(list_index);
		total = stream_count();
		name = s->name;
		body[n++] = (uint8_t)total;
		body[n++] = list_index;
		body[n++] = (uint8_t)s->rate_hz; body[n++] = (uint8_t)(s->rate_hz >> 8U);
		body[n++] = (uint8_t)(s->rate_hz >> 16U); body[n++] = (uint8_t)(s->rate_hz >> 24U);
		body[n++] = s->count;
		body[n++] = s->bytes;
	}
	size_t name_len = strlen(name);
	body[n++] = (uint8_t)name_len;
	memcpy(&body[n], name, name_len);
	return send_reply(list_cmd, STATUS_OK, body, (uint8_t)(n + name_len));
}

static void list_service(void)
{
	while (list_cmd != 0U && usb_cdc_tx_free() >= (int)LIST_MIN_FREE_BYTES) {
		size_t total = (list_cmd == CMD_LIST_VARS) ? var_count() : stream_count();
		if (list_index >= total) {
			list_cmd = 0U;
			break;
		}
		send_list_entry();
		++list_index;
	}
}

static void handle_read(uint16_t id)
{
	const var_t *v = var_find(id);
	uint8_t body[2 + VAR_MAX_BODY];
	if (v == NULL || (v->direction & VAR_DIR_TX) == 0U) {
		send_reply(CMD_READ, STATUS_ERROR, NULL, 0);
		return;
	}
	put_u16(body, id);
	memcpy(&body[2], v->ptr, v->size);
	send_reply(CMD_READ, STATUS_OK, body, (uint8_t)(2U + v->size));
}

static void handle_write(uint16_t id, const uint8_t *data, uint8_t length)
{
	const var_t *v = var_find(id);
	if (v == NULL || (v->direction & VAR_DIR_RX) == 0U || length != v->size) {
		send_reply(CMD_WRITE, STATUS_ERROR, NULL, 0);
		return;
	}
	memcpy(v->ptr, data, length);
	send_reply(CMD_WRITE, STATUS_OK, NULL, 0);
}

int protocol_log_write(const uint8_t *buf, uint16_t len)
{
	return protocol_send(FRAME_LOG, buf, len);
}

int protocol_log_format(const char *format, ...)
{
	char log_message[96];
	va_list arguments;
	uint16_t length = 0;

	va_start(arguments, format);
	while (*format != '\0' && length < sizeof(log_message) - 1) {
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

	return protocol_log_write((const uint8_t *)log_message, (uint16_t)length);
}

int protocol_bridge_poll(void)
{
	uint8_t frame[PROTOCOL_RX_BYTES];
	int available = usb_cdc_rx_available();
	int read_count = 0;

	if (available > 0) {
		read_count = usb_cdc_rx_read(frame, available > (int)sizeof(frame) ? (int)sizeof(frame) : available);
	}

	for (int pos = 0; pos < read_count; ) {
		uint8_t command = frame[pos++];
		if (command == CMD_LIST_VARS || command == CMD_LIST_STREAMS) {
			list_cmd = command;
			list_index = 0U;
			continue;
		}
		if (command != CMD_READ && command != CMD_WRITE) {
			continue;   /* resync on unknown bytes */
		}
		if (pos + 2 > read_count) {
			break;
		}
		uint16_t id = (uint16_t)frame[pos] | ((uint16_t)frame[pos + 1] << 8);
		pos += 2;

		if (command == CMD_READ) {
			handle_read(id);
			continue;
		}
		if (pos >= read_count) {
			break;
		}
		uint8_t length = frame[pos++];
		if (pos + length > read_count) {
			send_reply(CMD_WRITE, STATUS_ERROR, NULL, 0);
			break;
		}
		handle_write(id, &frame[pos], length);
		pos += length;
	}

	list_service();
	return read_count;
}
