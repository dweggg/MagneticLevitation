#ifndef PARAMETERS_H
#define PARAMETERS_H

#include <stddef.h>
#include <stdint.h>
#include <fix16.h>

typedef enum {
	PARAM_DIR_TX = 0x01,
	PARAM_DIR_RX = 0x02,
	PARAM_DIR_TX_RX = PARAM_DIR_TX | PARAM_DIR_RX
} parameter_direction_t;

typedef enum {
	PARAM_FMT_U8,
	PARAM_FMT_I8,
	PARAM_FMT_U16,
	PARAM_FMT_I16,
	PARAM_FMT_U32,
	PARAM_FMT_I32,
	PARAM_FMT_F16,
	PARAM_FMT_RAW
} parameter_format_t;

typedef struct {
	uint16_t id;
	const char *name;
	parameter_direction_t direction;
	parameter_format_t format;
	uint8_t size;
	const void *ptr;
} parameter_descriptor_t;

enum {
	PARAM_ID_TEMP_RAW = 0x0001,
	PARAM_ID_TARGET_CURRENT = 0x0002,
	PARAM_ID_ENABLE = 0x0003,
	PARAM_ID_DUTY_A = 0x0004,
	PARAM_ID_DUTY_B = 0x0005,
};

#define PARAM_CMD_READ   0x01
#define PARAM_CMD_WRITE  0x02
#define PARAM_CMD_LIST   0x03
#define PARAM_REPLY      0x80
#define PARAM_STATUS_OK    0x00
#define PARAM_STATUS_ERROR 0x01

void parameters_init(void);
const parameter_descriptor_t *parameters_map(size_t *count);
const parameter_descriptor_t *parameters_find(uint16_t id);
int parameters_publish_u8(uint16_t id, uint8_t value);
int parameters_publish_u16(uint16_t id, uint16_t value);
uint8_t parameters_get_enable(void);
fix16_t parameters_get_duty_a(void);
fix16_t parameters_get_duty_b(void);
int parameters_read_value(uint16_t id, uint8_t *buffer, uint8_t buffer_size);
int parameters_write_value(uint16_t id, const uint8_t *buffer, uint8_t length);
int parameters_bridge_poll(void);

#endif // PARAMETERS_H