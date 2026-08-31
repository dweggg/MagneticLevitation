#ifndef CONTROL_F16_H
#define CONTROL_F16_H

#include <fix16.h>

// fix16 implementation of a PID controller
typedef struct {
    fix16_t kp;           // Proportional gain
    fix16_t ki;           // Integral gain
    fix16_t kd;           // Derivative gain

    fix16_t ts;           // Sampling time

    fix16_t integral_k;   // Current integral
    fix16_t integral_k1;  // Previous integral
    fix16_t e_k;          // Current error
    fix16_t e_k1;         // Previous error

    fix16_t lim_p;        // Max output
    fix16_t lim_n;        // Min output

    fix16_t sp;           // Setpoint
    fix16_t fb;           // Feedback
    fix16_t out;          // Output
} pid_f16;

// Run a PID controller
void pid_f16_run(pid_f16 *pid);


#endif // CONTROL_F16_H