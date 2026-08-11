#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void protocol_init(void);
int protocol_process_byte(uint8_t b);

#ifdef __cplusplus
}
#endif

#endif /* PROTOCOL_H */
