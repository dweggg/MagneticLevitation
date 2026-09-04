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

typedef union {
	uint8_t u8;
	int8_t i8;
	uint16_t u16;
	int16_t i16;
	uint32_t u32;
	int32_t i32;
	fix16_t f16;
	uint8_t raw[sizeof(fix16_t)];
} parameter_value_t;

typedef struct {
	uint16_t id;
	const char *name;
	parameter_direction_t direction;
	parameter_format_t format;
	uint8_t size;
	parameter_value_t value;
} parameter_descriptor_t;

enum {
	PARAM_ID_TEMP_RAW = 0x0001,
	PARAM_ID_ENABLE = 0x0002,
	PARAM_ID_DUTY_A = 0x0003,
	PARAM_ID_DUTY_B = 0x0004,
	PARAM_ID_CPU = 0x0005,
	PARAM_ID_MAG_RAW = 0x0006,
	PARAM_ID_V_MEAS = 0x0007,
	PARAM_ID_I_REF = 0x0008,
	PARAM_ID_I_MEAS = 0x0009,
	PARAM_ID_I_FB = 0x000A,
};

void parameters_init(void);
parameter_descriptor_t *parameters_map(size_t *count);
parameter_descriptor_t *parameters_find(uint16_t id);
int parameters_publish(uint16_t id, const void *value);
int parameters_fetch(uint16_t id, void *buffer, uint8_t buffer_size);

#endif // PARAMETERS_H