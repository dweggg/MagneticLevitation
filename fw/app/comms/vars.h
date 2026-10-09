#ifndef VARS_H
#define VARS_H

/*
 * Unified variable registry: parameters (host writable), monitors (host
 * readable) and telemetry streams all share one descriptor.
 *
 * Any module registers a variable at init time with its name, format and a
 * pointer to the storage it owns. IDs are assigned dynamically and are
 * discovered by the host via the LIST commands, so nothing is hard-coded.
 * The module reads/writes its own storage directly: a host write lands in
 * the pointer, a host read or a stream sample reads from it.
 *
 * Streams are named groups of variables sampled together by whichever task
 * calls vars_stream_emit(). Every module can create its own stream at its own
 * rate; bandwidth is simply sum(bytes per sample * emit rate).
 *
 *     static fix16_t i_fb;
 *     stream_id_t s = vars_create_stream("current", 5000);
 *     vars_register("i_fb", VAR_F16, VAR_DIR_TX, &i_fb, s);   // streamed
 *     vars_register("kp",   VAR_F16, VAR_DIR_TX_RX, &kp, VARS_STREAM_NONE); // param
 *     ...
 *     void module_task(void) { ...; vars_stream_emit(s); }
 */

#include <stddef.h>
#include <stdint.h>

#define VARS_MAX_VARIABLE_COUNT        56U
#define VARS_MAX_STREAM_COUNT          4U
#define VARS_STREAM_MAX_VARIABLES     40U
#define VARS_STREAM_MAX_PAYLOAD_BYTES 192U /* sample payload, excluding id + tick */
#define VARS_MAX_NAME_CHARS           24U
#define VARS_MAX_BODY_BYTES           48U /* max bytes of a single variable value / list entry body */

#define VARS_STREAM_NONE 0xFFU

typedef enum {
	VAR_DIR_TX = 0x01,   /* device -> host: host may read */
	VAR_DIR_RX = 0x02,   /* host -> device: host may write */
	VAR_DIR_TX_RX = VAR_DIR_TX | VAR_DIR_RX
} var_direction_t;

typedef enum {
	VAR_U8, VAR_I8, VAR_U16, VAR_I16, VAR_U32, VAR_I32, VAR_F16, VAR_RAW
} var_format_t;

typedef uint16_t var_id_t;
typedef uint8_t stream_id_t;

#define VARS_ID_INVALID 0U

typedef struct {
	var_id_t id;
	const char *name;
	uint8_t direction;
	uint8_t format;
	uint8_t size;
	uint8_t stream;   /* VARS_STREAM_NONE if not streamed */
	uint8_t offset;   /* byte offset in stream sample payload */
	void *ptr;
} var_t;

typedef struct {
	const char *name;
	uint32_t rate_hz;   /* nominal, informational */
	uint8_t count;
	uint8_t bytes;
	uint8_t members[VARS_STREAM_MAX_VARIABLES];  /* indices into the var table */
} stream_t;

/* Registration. Size is derived from the format (use vars_register_raw for RAW). */
var_id_t vars_register(const char *name, var_format_t format, uint8_t direction,
					   void *ptr, stream_id_t stream);
var_id_t vars_register_raw(const char *name, uint8_t direction, void *ptr, uint8_t size);
stream_id_t vars_create_stream(const char *name, uint32_t rate_hz);

/* Convenience wrappers for the common cases. */
#define VARS_MONITOR(name, fmt, ptr, stream) vars_register(name, fmt, VAR_DIR_TX, ptr, stream)
#define VARS_PARAM(name, fmt, ptr)           vars_register(name, fmt, VAR_DIR_TX_RX, ptr, VARS_STREAM_NONE)

/* Lookup / iteration (used by the protocol layer). */
const var_t *vars_find(var_id_t id);
const var_t *vars_get_variable_at(size_t index);
size_t vars_get_variable_count(void);
const stream_t *vars_get_stream_at(size_t index);
size_t vars_get_stream_count(void);

/* Sample a stream now: copies subscribed member variables and queues one
 * frame stamped with the firmware tick. Never blocks; drops (and counts) if
 * the transmit ring cannot take the whole frame. */
void vars_stream_emit(stream_id_t stream);
void vars_stream_emit_at(stream_id_t stream, uint32_t tick);

/* Registers the built-in stream diagnostics (tick_hz, stream_dropped). */
void vars_init(void);

#endif /* VARS_H */
