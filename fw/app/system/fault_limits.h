#ifndef FAULT_LIMITS_H
#define FAULT_LIMITS_H

/* Provisional Q16.16 limits. Validate against the assembled hardware before enabling power. */
#define FAULT_OVERTEMPERATURE_LIMIT_C_Q16       F16(60.0f)  /* 60°C */
#define FAULT_OVERCURRENT_LIMIT_A_Q16           F16(25.0f)  /* 25A */
#define FAULT_OVERVOLTAGE_LIMIT_V_Q16           F16(22.0f)  /* 22V */

#endif // FAULT_LIMITS_H