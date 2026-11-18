#include "vars.h"
#include "protocol.h"
#include "scheduler.h"
#include "funconfig.h"
#include "ch32fun.h"

#include <string.h>

static var_t var_table[VARS_MAX_VARIABLE_COUNT];
static stream_t stream_table[VARS_MAX_STREAM_COUNT];
static size_t var_total;
static size_t stream_total;

static uint32_t tick_hz;
static uint32_t stream_dropped;
static uint8_t sample[5U + VARS_STREAM_MAX_PAYLOAD_BYTES]; /* stream id, tick, data */

static uint8_t vars_format_size(var_format_t f)
{
	switch (f) {
	case VAR_U8: case VAR_I8: return 1U;
	case VAR_U16: case VAR_I16: return 2U;
	case VAR_U32: case VAR_I32: case VAR_F16: return 4U;
	default: return 0U;
	}
}

static var_id_t vars_register_variable(const char *name, uint8_t format, uint8_t direction,
									   void *ptr, uint8_t size, stream_id_t stream)
{
	if (var_total >= VARS_MAX_VARIABLE_COUNT || name == NULL || ptr == NULL || size == 0U || size > 32U ||
	    strlen(name) > VARS_MAX_NAME_CHARS) {
		return VARS_ID_INVALID;
	}

	uint8_t offset = 0U;
	if (stream != VARS_STREAM_NONE) {
		stream_t *s = (stream < stream_total) ? &stream_table[stream] : NULL;
		if (s == NULL || s->count >= VARS_STREAM_MAX_VARIABLES || s->bytes + size > VARS_STREAM_MAX_PAYLOAD_BYTES) {
			return VARS_ID_INVALID;
		}
		offset = s->bytes;
		s->members[s->count++] = (uint8_t)var_total;
		s->bytes = (uint8_t)(s->bytes + size);
		direction |= VAR_DIR_TX;   /* anything streamed is also readable */
	}

	var_t *v = &var_table[var_total];
	v->id = (var_id_t)(var_total + 1U);   /* dynamic; 0 is reserved as invalid */
	v->name = name;
	v->direction = direction;
	v->format = format;
	v->size = size;
	v->stream = stream;
	v->offset = offset;
	v->ptr = ptr;
	++var_total;
	return v->id;
}

var_id_t vars_register(const char *name, var_format_t format, uint8_t direction,
                       void *ptr, stream_id_t stream)
{
	return vars_register_variable(name, format, direction, ptr, vars_format_size(format), stream);
}

var_id_t vars_register_raw(const char *name, uint8_t direction, void *ptr, uint8_t size)
{
	return vars_register_variable(name, VAR_RAW, direction, ptr, size, VARS_STREAM_NONE);
}

stream_id_t vars_create_stream(const char *name, uint32_t rate_hz)
{
	if (stream_total >= VARS_MAX_STREAM_COUNT) {
		return VARS_STREAM_NONE;
	}
	stream_t *s = &stream_table[stream_total];
	memset(s, 0, sizeof(*s));
	s->name = name;
	s->rate_hz = rate_hz;
	return (stream_id_t)stream_total++;
}

const var_t *vars_find(var_id_t id)
{
	return (id >= 1U && id <= var_total) ? &var_table[id - 1U] : NULL;
}

const var_t *vars_get_variable_at(size_t index) { return index < var_total ? &var_table[index] : NULL; }
size_t vars_get_variable_count(void) { return var_total; }
const stream_t *vars_get_stream_at(size_t index) { return index < stream_total ? &stream_table[index] : NULL; }
size_t vars_get_stream_count(void) { return stream_total; }

void vars_stream_emit_at(stream_id_t stream, uint32_t tick)
{
	if (stream >= stream_total) {
		return;
	}
	const stream_t *s = &stream_table[stream];
	if (s->count == 0U) {
		return;
	}

	sample[0] = stream;
	sample[1] = (uint8_t)tick;
	sample[2] = (uint8_t)(tick >> 8U);
	sample[3] = (uint8_t)(tick >> 16U);
	sample[4] = (uint8_t)(tick >> 24U);
	uint8_t data_bytes = 0U;
	for (uint8_t i = 0; i < s->count; ++i) {
		const var_t *v = &var_table[s->members[i]];
		if (protocol_stream_variable_enabled(v->id)) {
			memcpy(&sample[5U + data_bytes], v->ptr, v->size);
			data_bytes = (uint8_t)(data_bytes + v->size);
		}
	}
	if (data_bytes == 0U) {
		return;
	}

	if (protocol_send(PROTOCOL_FRAME_STREAM, sample, (uint16_t)(5U + data_bytes)) == 0) {
		++stream_dropped;
	}
}

void vars_stream_emit(stream_id_t stream)
{
	vars_stream_emit_at(stream, scheduler_get_tick());
}

void vars_init(void)
{
	tick_hz = FUNCONF_SYSTEM_CORE_CLOCK;   /* SysTick runs from HCLK */
	vars_register("tick_hz", VAR_U32, VAR_DIR_TX, &tick_hz, VARS_STREAM_NONE);
	vars_register("stream_dropped", VAR_U32, VAR_DIR_TX, &stream_dropped, VARS_STREAM_NONE);
}
