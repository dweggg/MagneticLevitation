#include "vars.h"
#include "protocol.h"
#include "scheduler.h"
#include "funconfig.h"
#include "ch32fun.h"

#include <string.h>

static var_t var_table[VARS_MAX];
static stream_t stream_table[STREAMS_MAX];
static size_t var_total;
static size_t stream_total;

static uint32_t tick_hz;
static uint32_t stream_dropped;
static uint8_t sample[5U + STREAM_MAX_BYTES];   /* stream id, tick, data */

static uint8_t format_size(var_format_t f)
{
	switch (f) {
	case VAR_U8: case VAR_I8: return 1U;
	case VAR_U16: case VAR_I16: return 2U;
	case VAR_U32: case VAR_I32: case VAR_F16: return 4U;
	default: return 0U;
	}
}

static var_id_t register_var(const char *name, uint8_t format, uint8_t direction,
                             void *ptr, uint8_t size, stream_id_t stream)
{
	if (var_total >= VARS_MAX || name == NULL || ptr == NULL || size == 0U || size > 32U ||
	    strlen(name) > VAR_NAME_MAX) {
		return VAR_ID_INVALID;
	}

	uint8_t offset = 0U;
	if (stream != STREAM_NONE) {
		stream_t *s = (stream < stream_total) ? &stream_table[stream] : NULL;
		if (s == NULL || s->count >= STREAM_MAX_VARS || s->bytes + size > STREAM_MAX_BYTES) {
			return VAR_ID_INVALID;
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

var_id_t var_register(const char *name, var_format_t format, uint8_t direction,
                      void *ptr, stream_id_t stream)
{
	return register_var(name, format, direction, ptr, format_size(format), stream);
}

var_id_t var_register_raw(const char *name, uint8_t direction, void *ptr, uint8_t size)
{
	return register_var(name, VAR_RAW, direction, ptr, size, STREAM_NONE);
}

stream_id_t stream_create(const char *name, uint32_t rate_hz)
{
	if (stream_total >= STREAMS_MAX) {
		return STREAM_NONE;
	}
	stream_t *s = &stream_table[stream_total];
	memset(s, 0, sizeof(*s));
	s->name = name;
	s->rate_hz = rate_hz;
	return (stream_id_t)stream_total++;
}

const var_t *var_find(var_id_t id)
{
	return (id >= 1U && id <= var_total) ? &var_table[id - 1U] : NULL;
}

const var_t *var_at(size_t index) { return index < var_total ? &var_table[index] : NULL; }
size_t var_count(void) { return var_total; }
const stream_t *stream_at(size_t index) { return index < stream_total ? &stream_table[index] : NULL; }
size_t stream_count(void) { return stream_total; }

void stream_emit_at(stream_id_t stream, uint32_t tick)
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
	for (uint8_t i = 0; i < s->count; ++i) {
		const var_t *v = &var_table[s->members[i]];
		memcpy(&sample[5U + v->offset], v->ptr, v->size);
	}

	if (protocol_send(FRAME_STREAM, sample, (uint16_t)(5U + s->bytes)) == 0) {
		++stream_dropped;
	}
}

void stream_emit(stream_id_t stream)
{
	stream_emit_at(stream, scheduler_get_tick());
}

void vars_init(void)
{
	tick_hz = FUNCONF_SYSTEM_CORE_CLOCK;   /* SysTick runs from HCLK */
	var_register("tick_hz", VAR_U32, VAR_DIR_TX, &tick_hz, STREAM_NONE);
	var_register("stream_dropped", VAR_U32, VAR_DIR_TX, &stream_dropped, STREAM_NONE);
}
