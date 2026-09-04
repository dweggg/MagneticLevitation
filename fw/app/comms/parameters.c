#include "parameters.h"
#include <string.h>

static parameter_descriptor_t parameter_map_table[] = {
	{
		.id = PARAM_ID_TEMP_RAW,
		.name = "temp_raw",
		.direction = PARAM_DIR_TX,
		.format = PARAM_FMT_U16,
		.size = sizeof(uint16_t),
		.value.u16 = 0,
	},
	{
		.id = PARAM_ID_ENABLE,
		.name = "enable",
		.direction = PARAM_DIR_RX,
		.format = PARAM_FMT_U8,
		.size = sizeof(uint8_t),
		.value.u8 = 0,
	},
	{
		.id = PARAM_ID_DUTY_A,
		.name = "duty_a",
		.direction = PARAM_DIR_RX,
		.format = PARAM_FMT_F16,
		.size = sizeof(fix16_t),
		.value.f16 = 0,
	},
	{
		.id = PARAM_ID_DUTY_B,
		.name = "duty_b",
		.direction = PARAM_DIR_RX,
		.format = PARAM_FMT_F16,
		.size = sizeof(fix16_t),
		.value.f16 = 0,
	},
	{
		.id = PARAM_ID_CPU,
		.name = "cpu",
		.direction = PARAM_DIR_TX,
		.format = PARAM_FMT_U8,
		.size = sizeof(uint8_t),
		.value.u8 = 0,
	},
	{
		.id = PARAM_ID_MAG_RAW,
		.name = "mag_raw",
		.direction = PARAM_DIR_TX,
		.format = PARAM_FMT_U16,
		.size = sizeof(uint16_t),
		.value.u16 = 0,
	},
	{
		.id = PARAM_ID_V_MEAS,
		.name = "v_meas",
		.direction = PARAM_DIR_TX,
		.format = PARAM_FMT_F16,
		.size = sizeof(fix16_t),
		.value.f16 = 0,
	},
	{
		.id = PARAM_ID_I_REF,
		.name = "i_ref",
		.direction = PARAM_DIR_TX,
		.format = PARAM_FMT_U16,
		.size = sizeof(uint16_t),
		.value.u16 = 0,
	},
	{
		.id = PARAM_ID_I_MEAS,
		.name = "i_meas",
		.direction = PARAM_DIR_TX,
		.format = PARAM_FMT_U16,
		.size = sizeof(uint16_t),
		.value.u16 = 0,
	},
	{
		.id = PARAM_ID_I_FB,
		.name = "i_fb",
		.direction = PARAM_DIR_TX,
		.format = PARAM_FMT_F16,
		.size = sizeof(fix16_t),
		.value.f16 = 0,
	},
	{
        .id = PARAM_ID_PWM_SWITCHING_FREQUENCY,
        .name = "pwm_switching_frequency",
        .direction = PARAM_DIR_TX_RX,
        .format = PARAM_FMT_U32,
        .size = sizeof(uint32_t),
        .value.u32 = 20000U,
    },
    {
        .id = PARAM_ID_CURRENT_KP,
        .name = "current_kp",
        .direction = PARAM_DIR_TX_RX,
        .format = PARAM_FMT_F16,
        .size = sizeof(fix16_t),
        .value.f16 = 0,
    },
    {
        .id = PARAM_ID_CURRENT_KI,
        .name = "current_ki",
        .direction = PARAM_DIR_TX_RX,
        .format = PARAM_FMT_F16,
        .size = sizeof(fix16_t),
        .value.f16 = 0,
    },
};

void parameters_init(void)
{
	for (size_t i = 0; i < (sizeof(parameter_map_table) / sizeof(parameter_map_table[0])); ++i) {
		memset(&parameter_map_table[i].value, 0, sizeof(parameter_map_table[i].value));
	}
}

parameter_descriptor_t *parameters_map(size_t *count)
{
	if (count != NULL) {
		*count = sizeof(parameter_map_table) / sizeof(parameter_map_table[0]);
	}
	return parameter_map_table;
}

parameter_descriptor_t *parameters_find(uint16_t id)
{
	for (size_t i = 0; i < (sizeof(parameter_map_table) / sizeof(parameter_map_table[0])); ++i) {
		if (parameter_map_table[i].id == id) {
			return &parameter_map_table[i];
		}
	}
	return NULL;
}

int parameters_publish(uint16_t id, const void *value)
{
	parameter_descriptor_t *desc = parameters_find(id);
	if (desc == NULL || value == NULL) {
		return -1;
	}

	switch (desc->format) {
	case PARAM_FMT_U8:
		desc->value.u8 = *(const uint8_t *)value;
		return 0;
	case PARAM_FMT_U16:
		desc->value.u16 = *(const uint16_t *)value;
		return 0;
    case PARAM_FMT_U32:
        desc->value.u32 = *(const uint32_t *)value;
        return 0;
	case PARAM_FMT_F16:
	{
		const fix16_t incoming = *(const fix16_t *)value;
		desc->value.f16 = incoming;
		return 0;
	}
	default:
		return -2;
	}
}

int parameters_fetch(uint16_t id, void *buffer, uint8_t buffer_size)
{
	parameter_descriptor_t *desc = parameters_find(id);
	if (desc == NULL || buffer == NULL || buffer_size < desc->size) {
		return -1;
	}

	switch (desc->format) {
	case PARAM_FMT_U8:
		*(uint8_t *)buffer = desc->value.u8;
		return (int)desc->size;
	case PARAM_FMT_U16:
		*(uint16_t *)buffer = desc->value.u16;
		return (int)desc->size;
	case PARAM_FMT_U32:
        *(uint32_t *)buffer = desc->value.u32;
        return (int)desc->size;
	case PARAM_FMT_F16:
		memcpy(buffer, &desc->value.f16, sizeof(fix16_t));
		return (int)desc->size;
	default:
		return -2;
	}
}
