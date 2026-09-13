#ifndef TELEMETRY_H
#define TELEMETRY_H

#include <stdint.h>
#include <fix16.h>

/*
 * Wire frame (20 bytes):
 * [0xA5][0x5A][sequence][i_fb][v_meas][duty_a][duty_b][xor checksum]
 *
 * Each value is a signed Q16.16 value encoded little-endian. The checksum is
 * the XOR of every preceding byte in the frame. Hosts can resynchronize by
 * scanning for the two-byte sync word and validating the fixed-size frame.
 */
#define TELEMETRY_CHANNEL_COUNT 4U
#define TELEMETRY_FRAME_BYTES   (2U + 1U + (TELEMETRY_CHANNEL_COUNT * sizeof(fix16_t)) + 1U)

/* Publish stream metadata after the parameter map has been initialized. */
void telemetry_init(void);

/*
 * Queue one complete telemetry sample. This function never blocks; a sample
 * is discarded if the CDC transmit ring cannot accept the entire frame.
 */
void telemetry_capture(const fix16_t *values, uint8_t count);

#endif /* TELEMETRY_H */
