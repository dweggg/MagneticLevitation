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
 * calls stream_emit(). Every module can create its own stream at its own
 * rate; bandwidth is simply sum(bytes per sample * emit rate).
 *
 *     static fix16_t i_fb;
 *     stream_id_t s = stream_create("current", 5000);
 *     var_register("i_fb", VAR_F16, VAR_DIR_TX, &i_fb, s);   // streamed
 *     var_register("kp",   VAR_F16, VAR_DIR_TX_RX, &kp, STREAM_NONE); // param
 *     ...
 *     void task(void) { ...; stream_emit(s); }
 */

#include <stddef.h>
#include <stdint.h>

#define VARS_MAX             48U
#define STREAMS_MAX          4U
#define STREAM_MAX_VARS      40U
#define STREAM_MAX_BYTES     192U  /* sample payload, excluding id + tick */
#define VAR_NAME_MAX         24U
#define VAR_MAX_BODY         48U   /* max bytes of a single variable value / list entry body */

#define STREAM_NONE          0xFFU

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

#define VAR_ID_INVALID 0U

typedef struct {
	var_id_t id;
	const char *name;
	uint8_t direction;
	uint8_t format;
	uint8_t size;
	uint8_t stream;   /* STREAM_NONE if not streamed */
	uint8_t offset;   /* byte offset in stream sample payload */
	void *ptr;
} var_t;

typedef struct {
	const char *name;
	uint32_t rate_hz;   /* nominal, informational */
	uint8_t count;
	uint8_t bytes;
	uint8_t members[STREAM_MAX_VARS];  /* indices into the var table */
} stream_t;

/* Registration. size is derived from the format (use var_register_raw for RAW). */
var_id_t var_register(const char *name, var_format_t format, uint8_t direction,
                      void *ptr, stream_id_t stream);
var_id_t var_register_raw(const char *name, uint8_t direction, void *ptr, uint8_t size);
stream_id_t stream_create(const char *name, uint32_t rate_hz);

/* Convenience wrappers for the common cases. */
#define var_monitor(name, fmt, ptr, stream) var_register(name, fmt, VAR_DIR_TX, ptr, stream)
#define var_param(name, fmt, ptr)           var_register(name, fmt, VAR_DIR_TX_RX, ptr, STREAM_NONE)

/* Lookup / iteration (used by the protocol layer). */
const var_t *var_find(var_id_t id);
const var_t *var_at(size_t index);
size_t var_count(void);
const stream_t *stream_at(size_t index);
size_t stream_count(void);

/* Sample a stream now: copies every member variable and queues one frame
 * stamped with the firmware tick. Never blocks; drops (and counts) if the
 * transmit ring cannot take the whole frame. */
void stream_emit(stream_id_t stream);
void stream_emit_at(stream_id_t stream, uint32_t tick);

/* Registers the built-in stream diagnostics (tick_hz, stream_dropped). */
void vars_init(void);

#endif /* VARS_H */
