#include "telemetry.h"

#include "parameters.h"
#include "tasks.h"
#include "usb_cdc.h"

#define TELEMETRY_SYNC_0 0xA5U
#define TELEMETRY_SYNC_1 0x5AU

static uint8_t telemetry_sequence;
static uint32_t telemetry_dropped_frames;
static const char telemetry_variable_names[] = "i_fb,i_sp,v_out,duty_a";

static void put_u32_le(uint8_t *buffer, uint32_t value)
{
    buffer[0] = (uint8_t)value;
    buffer[1] = (uint8_t)(value >> 8U);
    buffer[2] = (uint8_t)(value >> 16U);
    buffer[3] = (uint8_t)(value >> 24U);
}

void telemetry_init(void)
{
    const uint8_t channel_count = TELEMETRY_CHANNEL_COUNT;
    const uint32_t rate_hz = TASK_CURRENT_CONTROL_HZ;

    telemetry_sequence = 0;
    telemetry_dropped_frames = 0;
    parameters_publish(PARAM_ID_STREAM_CHANNELS, &channel_count);
    parameters_publish(PARAM_ID_STREAM_RATE_HZ, &rate_hz);
    parameters_publish(PARAM_ID_STREAM_DROPPED, &telemetry_dropped_frames);
    parameter_descriptor_t *name_descriptor = parameters_find(PARAM_ID_STREAM_VARIABLE_NAMES);
    if (name_descriptor != NULL) {
        name_descriptor->size = (uint8_t)(sizeof(telemetry_variable_names) - 1U);
        parameters_publish(PARAM_ID_STREAM_VARIABLE_NAMES, telemetry_variable_names);
    }
}

void telemetry_capture(const fix16_t *values, uint8_t count)
{
    uint8_t frame[TELEMETRY_FRAME_BYTES];
    uint8_t checksum = 0;

    if (values == NULL || count != TELEMETRY_CHANNEL_COUNT ||
        usb_cdc_tx_free() < (int)TELEMETRY_FRAME_BYTES) {
        ++telemetry_dropped_frames;
        parameters_publish(PARAM_ID_STREAM_DROPPED, &telemetry_dropped_frames);
        return;
    }

    frame[0] = TELEMETRY_SYNC_0;
    frame[1] = TELEMETRY_SYNC_1;
    frame[2] = telemetry_sequence++;

    for (uint8_t channel = 0; channel < TELEMETRY_CHANNEL_COUNT; ++channel) {
        put_u32_le(&frame[3U + (channel * sizeof(fix16_t))],
                   (uint32_t)values[channel]);
    }

    for (uint8_t index = 0; index < TELEMETRY_FRAME_BYTES - 1U; ++index) {
        checksum ^= frame[index];
    }
    frame[TELEMETRY_FRAME_BYTES - 1U] = checksum;

    /* The prior free-space check guarantees an all-or-nothing queue operation. */
    (void)usb_cdc_tx_send(frame, (int)sizeof(frame));
}
