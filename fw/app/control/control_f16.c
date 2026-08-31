#include "control_f16.h"

void pid_f16_run(pid_f16_t *pid)
{
    // Calculate error
    pid->e_k = fix16_sub(pid->sp, pid->fb);

    // Calculate terms
    fix16_t p_term = fix16_mul(pid->kp, pid->e_k);
    fix16_t i_term = fix16_mul(pid->ki, fix16_mul(pid->e_k, pid->ts));
    fix16_t d_term = fix16_div(fix16_mul(pid->kd, (pid->e_k - pid->e_k1)), pid->ts);

    // Tentative integral accumulation
    fix16_t i_term_tent = fix16_add(pid->integral_k1, i_term);

    // Unsaturated output using the tentative integral
    fix16_t out_unsat = p_term + i_term_tent + d_term;

    // Anti-windup: freeze integrator if output is saturated
    if (out_unsat > pid->lim_p && i_term > 0) {
        pid->integral_k = pid->integral_k1;   // freeze integrator
    } else if (out_unsat < pid->lim_n && i_term < 0) {
        pid->integral_k = pid->integral_k1;   // freeze integrator
    } else {
        pid->integral_k = i_term_tent; // normal accumulation
    }

    // Final output
    pid->out = p_term + pid->integral_k + d_term;

    // Apply final saturation limits
    if (pid->out > pid->lim_p) {
        pid->out = pid->lim_p;
    } else if (pid->out < pid->lim_n) {
        pid->out = pid->lim_n;
    }

    // Update the stored values for the next iteration
    pid->integral_k1 = pid->integral_k;
    pid->e_k1 = pid->e_k;
}

