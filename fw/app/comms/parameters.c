#include "parameters.h"
#include "usb_cdc.h"
#include <string.h>

#define PARAM_MAX_FRAME_BYTES 32

static uint16_t parameter_temp_raw = 0;
static uint16_t parameter_target_current = 0;
static uint8_t parameter_enable = 0;
static fix16_t parameter_duty_a = 0;
static fix16_t parameter_duty_b = 0;

static const parameter_descriptor_t parameter_map_table[] = {
	{
		.id = PARAM_ID_TEMP_RAW,
		.name = "temp_raw",
		.direction = PARAM_DIR_TX,
		.format = PARAM_FMT_U16,
		.size = sizeof(uint16_t),
		.ptr = &parameter_temp_raw,
	},
	{
		.id = PARAM_ID_TARGET_CURRENT,
		.name = "target_current",
		.direction = PARAM_DIR_RX,
		.format = PARAM_FMT_U16,
		.size = sizeof(uint16_t),
		.ptr = &parameter_target_current,
	},
	{
		.id = PARAM_ID_ENABLE,
		.name = "enable",
		.direction = PARAM_DIR_TX_RX,
		.format = PARAM_FMT_U8,
		.size = sizeof(uint8_t),
		.ptr = &parameter_enable,
	},
	{
		.id = PARAM_ID_DUTY_A,
		.name = "duty_a",
		.direction = PARAM_DIR_TX_RX,
		.format = PARAM_FMT_F16,
		.size = sizeof(fix16_t),
		.ptr = &parameter_duty_a,
	},
	{
		.id = PARAM_ID_DUTY_B,
		.name = "duty_b",
		.direction = PARAM_DIR_TX_RX,
		.format = PARAM_FMT_F16,
		.size = sizeof(fix16_t),
		.ptr = &parameter_duty_b,
	},
};

static int parameters_send_frame(const uint8_t *payload, uint8_t length)
{
	uint8_t frame[PARAM_MAX_FRAME_BYTES];
	if (length + 1 > sizeof(frame)) {
		return 0;
	}

	frame[0] = PARAM_REPLY;
	memcpy(&frame[1], payload, length);
	return usb_cdc_tx_send(frame, (int)length + 1);
}

static int parameters_send_status_response(uint8_t status)
{
	return parameters_send_frame(&status, 1);
}

static int parameters_send_object_response(uint16_t id, const uint8_t *data, uint8_t length)
{
	uint8_t reply[4 + PARAM_MAX_FRAME_BYTES];
	uint8_t reply_len = 0;

	reply[reply_len++] = (uint8_t)(id & 0xFF);
	reply[reply_len++] = (uint8_t)((id >> 8) & 0xFF);
	reply[reply_len++] = length;
	memcpy(&reply[reply_len], data, length);
	reply_len += length;
	return parameters_send_frame(reply, reply_len);
}

static int parameters_send_list_response(void)
{
	const size_t count = sizeof(parameter_map_table) / sizeof(parameter_map_table[0]);
	uint8_t frame[1 + (sizeof(parameter_map_table) / sizeof(parameter_map_table[0])) * 5];
	uint8_t index = 0;

	frame[index++] = (uint8_t)count;
	for (size_t i = 0; i < count; ++i) {
		const parameter_descriptor_t *desc = &parameter_map_table[i];
		frame[index++] = (uint8_t)(desc->id & 0xFF);
		frame[index++] = (uint8_t)((desc->id >> 8) & 0xFF);
		frame[index++] = desc->direction;
		frame[index++] = desc->format;
		frame[index++] = desc->size;
	}
	return parameters_send_frame(frame, index);
}

static int parameters_set_u16_value(uint16_t id, uint16_t value)
{
	switch (id) {
	case PARAM_ID_TEMP_RAW:
		parameter_temp_raw = value;
		return 0;
	case PARAM_ID_TARGET_CURRENT:
		parameter_target_current = value;
		return 0;
	default:
		return -1;
	}
}

static int parameters_set_f16_value(uint16_t id, fix16_t value)
{
	switch (id) {
	case PARAM_ID_DUTY_A:
		parameter_duty_a = value;
		return 0;
	case PARAM_ID_DUTY_B:
		parameter_duty_b = value;
		return 0;
	default:
		return -1;
	}
}

static int parameters_set_u8_value(uint16_t id, uint8_t value)
{
	switch (id) {
	case PARAM_ID_ENABLE:
		parameter_enable = value;
		return 0;
	default:
		return -1;
	}
}

void parameters_init(void)
{
	parameter_temp_raw = 0;
	parameter_enable = 0;
	parameter_target_current = 0;
	parameter_duty_a = 0;
	parameter_duty_b = 0;
}

int parameters_publish_u8(uint16_t id, uint8_t value)
{
	return parameters_set_u8_value(id, value);
}

int parameters_publish_u16(uint16_t id, uint16_t value)
{
	return parameters_set_u16_value(id, value);
}

uint8_t parameters_get_enable(void)
{
	return parameter_enable;
}

fix16_t parameters_get_duty_a(void)
{
	return parameter_duty_a;
}

fix16_t parameters_get_duty_b(void)
{
	return parameter_duty_b;
}

const parameter_descriptor_t *parameters_map(size_t *count)
{
	if (count != NULL) {
		*count = sizeof(parameter_map_table) / sizeof(parameter_map_table[0]);
	}
	return parameter_map_table;
}

const parameter_descriptor_t *parameters_find(uint16_t id)
{
	for (size_t i = 0; i < (sizeof(parameter_map_table) / sizeof(parameter_map_table[0])); ++i) {
		if (parameter_map_table[i].id == id) {
			return &parameter_map_table[i];
		}
	}
	return NULL;
}

int parameters_read_value(uint16_t id, uint8_t *buffer, uint8_t buffer_size)
{
	const parameter_descriptor_t *desc = parameters_find(id);
	if (desc == NULL || buffer == NULL || buffer_size < desc->size) {
		return -1;
	}
	if ((desc->direction & PARAM_DIR_TX) == 0) {
		return -2;
	}

	switch (desc->format) {
	case PARAM_FMT_U8:
		if (desc->ptr == NULL) {
			return -3;
		}
		*(uint8_t *)desc->ptr = parameter_enable;
		buffer[0] = parameter_enable;
		return (int)desc->size;
	case PARAM_FMT_U16:
	{
		uint16_t value = 0;
		if (desc->ptr == NULL) {
			return -3;
		}
		value = *(uint16_t *)desc->ptr;
		buffer[0] = (uint8_t)(value & 0xFF);
		buffer[1] = (uint8_t)((value >> 8) & 0xFF);
		return (int)desc->size;
	}
	case PARAM_FMT_F16:
	{
		fix16_t value = 0;
		if (desc->ptr == NULL) {
			return -3;
		}
		value = *(fix16_t *)desc->ptr;
		memcpy(buffer, &value, sizeof(value));
		return (int)desc->size;
	}
	default:
		return -4;
	}
}

int parameters_write_value(uint16_t id, const uint8_t *buffer, uint8_t length)
{
	const parameter_descriptor_t *desc = parameters_find(id);
	if (desc == NULL || buffer == NULL || length != desc->size) {
		return -1;
	}
	if ((desc->direction & PARAM_DIR_RX) == 0) {
		return -2;
	}

	switch (desc->format) {
	case PARAM_FMT_U8:
		return parameters_set_u8_value(id, buffer[0]);
	case PARAM_FMT_U16:
	{
		uint16_t value = (uint16_t)buffer[0] | ((uint16_t)buffer[1] << 8);
		return parameters_set_u16_value(id, value);
	}
	case PARAM_FMT_F16:
	{
		fix16_t value = 0;
		memcpy(&value, buffer, sizeof(value));
		return parameters_set_f16_value(id, value);
	}
	default:
		return -3;
	}
}

int parameters_bridge_poll(void)
{
	uint8_t frame[PARAM_MAX_FRAME_BYTES];
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
			parameters_send_list_response();
			continue;
		}
		if (pos + 1 >= read_count) {
			break;
		}

		uint16_t id = (uint16_t)frame[pos] | ((uint16_t)frame[pos + 1] << 8);
		pos += 2;

		if (command == PARAM_CMD_READ) {
			uint8_t value_buffer[4] = {0};
			int result = parameters_read_value(id, value_buffer, sizeof(value_buffer));
			if (result >= 0) {
				parameters_send_object_response(id, value_buffer, (uint8_t)result);
			} else {
				parameters_send_status_response(PARAM_STATUS_ERROR);
			}
			continue;
		}

		if (command == PARAM_CMD_WRITE) {
			if (pos >= read_count) {
				parameters_send_status_response(PARAM_STATUS_ERROR);
				break;
			}
			uint8_t payload_length = frame[pos++];
			if (payload_length == 0 || pos + payload_length > read_count) {
				parameters_send_status_response(PARAM_STATUS_ERROR);
				break;
			}
			int write_result = parameters_write_value(id, &frame[pos], payload_length);
			if (write_result == 0) {
				parameters_send_status_response(PARAM_STATUS_OK);
			} else {
				parameters_send_status_response(PARAM_STATUS_ERROR);
			}
			pos += payload_length;
		}
	}

	return read_count;
}
