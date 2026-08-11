#ifndef PARAMETERS_H
#define PARAMETERS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Load or apply runtime parameters. */
void parameters_load(void);

/** Save current parameters to non-volatile storage. */
void parameters_save(void);

#ifdef __cplusplus
}
#endif

#endif /* PARAMETERS_H */
