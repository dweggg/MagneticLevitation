#include "protocol.h"
#include "usb_cdc.h"
#include "vars.h"
#include <stdarg.h>
#include <string.h>

#define PROTOCOL_RX_BUFFER_SIZE_BYTES 128U
#define PROTOCOL_LIST_MIN_FREE_BYTES 64U

static uint16_t protocol_append_text(char *buffer, uint16_t position, uint16_t size,
                            const char *text)
{
	while (*text != '\0' && position < size - 1) {
		buffer[position++] = *text++;
	}

	return position;
}

static uint16_t protocol_append_unsigned(char *buffer, uint16_t position, uint16_t size,
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

static uint16_t protocol_append_unsigned_long(char *buffer, uint16_t position, uint16_t size,
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

static uint16_t protocol_append_hex(char *buffer, uint16_t position, uint16_t size,
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
 * Frame types: PROTOCOL_FRAME_LOG (ASCII), PROTOCOL_FRAME_REPLY (command reply),
 * PROTOCOL_FRAME_STREAM ([stream id][u32 tick LE][sample bytes]).
 *
 * Host -> device commands are unframed and short:
 *   READ [id16], WRITE [id16][len][data], LIST_VARS, LIST_STREAMS,
 *   SET_STREAM_SUBSCRIPTION [count][id16...].
 * Replies are [cmd][status][body...]; list replies carry one entry per frame.
 */

#define PROTOCOL_TX_CONTROL_RESERVE_BYTES 128U

int protocol_send(uint8_t type, const uint8_t *payload, uint16_t length)
{
	int required = (int)length + 4;
	int reserve = (type == PROTOCOL_FRAME_STREAM) ? PROTOCOL_TX_CONTROL_RESERVE_BYTES : 0;
	if (length > 255U || usb_cdc_tx_free() < required + reserve) {
		return 0;   /* all-or-nothing: never emit a partial frame */
	}

	uint8_t header[3] = { PROTOCOL_FRAME_SYNC, type, (uint8_t)length };
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
	return 1;
}

static int protocol_send_reply(uint8_t cmd, uint8_t status, const uint8_t *body, uint8_t body_len)
{
	uint8_t reply[2 + VARS_MAX_BODY_BYTES];
	reply[0] = cmd;
	reply[1] = status;
	if (body_len != 0U) {
		memcpy(&reply[2], body, body_len);
	}
	return protocol_send(PROTOCOL_FRAME_REPLY, reply, (uint16_t)(2U + body_len));
}

static void protocol_put_u16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8U); }

/* Progressive LIST: entries are sent as ring space allows, so a long
 * list never overflows the transmit ring or fights the telemetry for it. */
static uint8_t list_cmd;    /* 0 = idle */
static uint8_t list_index;
static uint8_t stream_subscription_active;
static uint8_t subscribed_variables[VARS_MAX_VARIABLE_COUNT];
static uint8_t rx_pending[PROTOCOL_RX_BUFFER_SIZE_BYTES];
static uint16_t rx_pending_length;
static uint16_t rx_discard_bytes;

int protocol_stream_variable_enabled(uint16_t id)
{
	return stream_subscription_active != 0U && id != 0U &&
	       id <= VARS_MAX_VARIABLE_COUNT && subscribed_variables[id - 1U] != 0U;
}

static int protocol_send_list_entry(void)
{
	uint8_t body[8 + VARS_MAX_NAME_CHARS + 4];
	uint8_t n = 0;
	const char *name;
	size_t total;

	body[n++] = list_index;
	if (list_cmd == PROTOCOL_COMMAND_LIST_VARS) {
		const var_t *v = vars_get_variable_at(list_index);
		total = vars_get_variable_count();
		name = v->name;
		body[n++] = (uint8_t)total;
		protocol_put_u16(&body[n], v->id); n += 2;
		body[n++] = v->direction;
		body[n++] = v->format;
		body[n++] = v->size;
		body[n++] = v->stream;
		body[n++] = v->offset;
	} else {
		const stream_t *s = vars_get_stream_at(list_index);
		total = vars_get_stream_count();
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
	return protocol_send_reply(list_cmd, PROTOCOL_STATUS_OK, body, (uint8_t)(n + name_len));
}

static void protocol_service_list(void)
{
	if (list_cmd == 0U || usb_cdc_tx_free() < (int)PROTOCOL_LIST_MIN_FREE_BYTES) {
		return;
	}

	size_t total = (list_cmd == PROTOCOL_COMMAND_LIST_VARS) ? vars_get_variable_count() : vars_get_stream_count();
	if (list_index >= total) {
		list_cmd = 0U;
		return;
	}

	if (protocol_send_list_entry()) {
		++list_index;
	}
}

static void protocol_handle_read(uint16_t id)
{
	const var_t *v = vars_find(id);
	uint8_t body[2 + VARS_MAX_BODY_BYTES];
	if (v == NULL || (v->direction & VAR_DIR_TX) == 0U) {
		protocol_send_reply(PROTOCOL_COMMAND_READ, PROTOCOL_STATUS_ERROR, NULL, 0);
		return;
	}
	protocol_put_u16(body, id);
	memcpy(&body[2], v->ptr, v->size);
	protocol_send_reply(PROTOCOL_COMMAND_READ, PROTOCOL_STATUS_OK, body, (uint8_t)(2U + v->size));
}

static void protocol_handle_write(uint16_t id, const uint8_t *data, uint8_t length)
{
	const var_t *v = vars_find(id);
	if (v == NULL || (v->direction & VAR_DIR_RX) == 0U || length != v->size) {
		protocol_send_reply(PROTOCOL_COMMAND_WRITE, PROTOCOL_STATUS_ERROR, NULL, 0);
		return;
	}
	memcpy(v->ptr, data, length);
	protocol_send_reply(PROTOCOL_COMMAND_WRITE, PROTOCOL_STATUS_OK, NULL, 0);
}

static void protocol_handle_stream_subscription(const uint8_t *ids, uint8_t count)
{
	uint8_t selected[VARS_MAX_VARIABLE_COUNT] = {0};
	for (uint8_t i = 0; i < count; ++i) {
		uint16_t id = (uint16_t)ids[2U * i] | ((uint16_t)ids[2U * i + 1U] << 8U);
		const var_t *v = vars_find(id);
		if (v == NULL || v->stream == VARS_STREAM_NONE) {
			protocol_send_reply(PROTOCOL_COMMAND_SET_STREAM_SUBSCRIPTION, PROTOCOL_STATUS_ERROR, NULL, 0);
			return;
		}
		selected[id - 1U] = 1U;
	}

	memcpy(subscribed_variables, selected, sizeof(subscribed_variables));
	stream_subscription_active = 1U;
	protocol_send_reply(PROTOCOL_COMMAND_SET_STREAM_SUBSCRIPTION, PROTOCOL_STATUS_OK, NULL, 0);
}

int protocol_log_write(const uint8_t *buf, uint16_t len)
{
	return protocol_send(PROTOCOL_FRAME_LOG, buf, len);
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
				length = protocol_append_unsigned_long(log_message, length, sizeof(log_message),
				                            va_arg(arguments, unsigned long));
			} else {
				length = protocol_append_unsigned(log_message, length, sizeof(log_message),
				                       va_arg(arguments, unsigned int));
			}
			break;
		case 'x':
			if (is_long) {
				length = protocol_append_hex(log_message, length, sizeof(log_message),
				                   va_arg(arguments, unsigned long));
			} else {
				length = protocol_append_hex(log_message, length, sizeof(log_message),
				                   va_arg(arguments, unsigned int));
			}
			break;
		case 'X':
			if (is_long) {
				length = protocol_append_hex(log_message, length, sizeof(log_message),
				                   va_arg(arguments, unsigned long));
			} else {
				length = protocol_append_hex(log_message, length, sizeof(log_message),
				                   va_arg(arguments, unsigned int));
			}
			break;
		case 's':
			length = protocol_append_text(log_message, length, sizeof(log_message),
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
	int available = usb_cdc_rx_available();
	int read_count = 0;

	if (available > 0 && rx_pending_length < sizeof(rx_pending)) {
		int room = (int)sizeof(rx_pending) - rx_pending_length;
		read_count = usb_cdc_rx_read(&rx_pending[rx_pending_length],
		                             available > room ? room : available);
		rx_pending_length = (uint16_t)(rx_pending_length + read_count);
	}

	uint16_t pos = 0U;
	while (pos < rx_pending_length) {
		if (rx_discard_bytes != 0U) {
			uint16_t remaining = (uint16_t)(rx_pending_length - pos);
			uint16_t discard = remaining < rx_discard_bytes ? remaining : rx_discard_bytes;
			pos = (uint16_t)(pos + discard);
			rx_discard_bytes = (uint16_t)(rx_discard_bytes - discard);
			continue;
		}

		uint8_t command = rx_pending[pos];
		if (command == PROTOCOL_COMMAND_LIST_VARS || command == PROTOCOL_COMMAND_LIST_STREAMS) {
			++pos;
			list_cmd = command;
			list_index = 0U;
			continue;
		}
		if (command == PROTOCOL_COMMAND_SET_STREAM_SUBSCRIPTION) {
			if (rx_pending_length - pos < 2U) {
				break;
			}
			uint8_t count = rx_pending[pos + 1U];
			uint16_t command_length = (uint16_t)(2U + 2U * count);
			if (count > VARS_MAX_VARIABLE_COUNT) {
				protocol_send_reply(PROTOCOL_COMMAND_SET_STREAM_SUBSCRIPTION,
				                    PROTOCOL_STATUS_ERROR, NULL, 0);
				pos = (uint16_t)(pos + 2U);
				rx_discard_bytes = (uint16_t)(2U * count);
				continue;
			}
			if (rx_pending_length - pos < command_length) {
				break;
			}
			protocol_handle_stream_subscription(&rx_pending[pos + 2U], count);
			pos = (uint16_t)(pos + command_length);
			continue;
		}
		if (command != PROTOCOL_COMMAND_READ && command != PROTOCOL_COMMAND_WRITE) {
			++pos;   /* resync on unknown bytes */
			continue;
		}
		if (rx_pending_length - pos < 3U) {
			break;
		}
		uint16_t id = (uint16_t)rx_pending[pos + 1U] | ((uint16_t)rx_pending[pos + 2U] << 8U);

		if (command == PROTOCOL_COMMAND_READ) {
			protocol_handle_read(id);
			pos = (uint16_t)(pos + 3U);
			continue;
		}
		if (rx_pending_length - pos < 4U) {
			break;
		}
		uint8_t length = rx_pending[pos + 3U];
		if (length > VARS_MAX_BODY_BYTES) {
			protocol_send_reply(PROTOCOL_COMMAND_WRITE, PROTOCOL_STATUS_ERROR, NULL, 0);
			pos = (uint16_t)(pos + 4U);
			rx_discard_bytes = length;
			continue;
		}
		uint16_t command_length = (uint16_t)(4U + length);
		if (rx_pending_length - pos < command_length) {
			break;
		}
		protocol_handle_write(id, &rx_pending[pos + 4U], length);
		pos = (uint16_t)(pos + command_length);
	}

	if (pos != 0U) {
		rx_pending_length = (uint16_t)(rx_pending_length - pos);
		memmove(rx_pending, &rx_pending[pos], rx_pending_length);
	}

	protocol_service_list();
	return read_count;
}
