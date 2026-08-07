#!/usr/bin/env python3
"""
Complementary PWM PWL generator for SPICE (LTspice, ngspice, PSpice, etc.)

Generates two piecewise-linear gate-drive waveforms (HIGH-side and LOW-side)
with finite rise/fall times and dead time between complementary transitions,
ready to be used directly as SPICE PWL sources.

Timing convention
------------------
Within each period T = 1/freq:
  1. HIGH-side signal rises at t = k*T,            reaches Vhigh after trise
  2. HIGH-side signal falls at t = k*T + Thigh,     reaches Vlow  after tfall
  3. LOW-side  signal rises at t = k*T + Thigh + deadtime
  4. LOW-side  signal falls at t = (k+1)*T - deadtime

So each period has TWO dead-time gaps (HIGH->LOW transition and LOW->HIGH
transition) where both outputs are low. This requires:
      2 * deadtime < (T - Thigh)     i.e. dead-time must fit inside the "off" time

Usage
-----
python pwm_pwl_gen.py --freq 100e3 --duty 0.5 --deadtime 100e-9 \
    --rise-fall 10e-9 --vhigh 12 --vlow 0 --cycles 10

python pwm_pwl_gen.py --freq 50e3 --duty 0.3 --deadtime 200e-9 \
    --trise 20e-9 --tfall 15e-9 --vhigh 15 --vlow -5 --cycles 5
"""

import argparse
import sys


def build_channel(edges, trise, tfall, vlow, vhigh, t_stop):
    """
    edges: sorted list of (time, kind) where kind is 'rise' or 'fall',
           marking when a ramp BEGINS (signal starts changing at that instant).
    Returns a list of (t, v) points forming the PWL waveform, starting at vlow.
    """
    points = [(0.0, vlow)]
    v_cursor = vlow

    for t_edge, kind in edges:
        if t_edge < points[-1][0] - 1e-15:
            raise ValueError(f"Overlapping/negative-time edge detected at t={t_edge:.3e}s")
        # flat segment holding the previous level up to the edge
        if t_edge > points[-1][0] + 1e-15:
            points.append((t_edge, v_cursor))
        if kind == 'rise':
            t_end, v_end = t_edge + trise, vhigh
        else:
            t_end, v_end = t_edge + tfall, vlow
        points.append((t_end, v_end))
        v_cursor = v_end

    if t_stop > points[-1][0]:
        points.append((t_stop, v_cursor))

    return points


def generate(freq, duty, deadtime, trise, tfall, vhigh, vlow, cycles, extra_time=0.0):
    T = 1.0 / freq
    Thigh = duty * T
    Tlow = T - Thigh

    if not (0.0 < duty < 1.0):
        raise ValueError("duty must be strictly between 0 and 1")
    if 2 * deadtime >= Tlow:
        raise ValueError(
            f"Deadtime too large: 2*deadtime ({2*deadtime*1e9:.1f} ns) must be < "
            f"off-time of the HIGH-side signal ({Tlow*1e9:.1f} ns). "
            f"Reduce deadtime, reduce duty, or lower frequency."
        )
    if trise > deadtime or tfall > deadtime:
        print(f"WARNING: rise/fall time ({trise*1e9:.1f}/{tfall*1e9:.1f} ns) exceeds "
              f"deadtime ({deadtime*1e9:.1f} ns) -> the two channels may briefly "
              f"overlap while ramping through the transition region.", file=sys.stderr)

    edges_hi, edges_lo = [], []
    for k in range(cycles):
        t0 = k * T
        edges_hi.append((t0, 'rise'))
        edges_hi.append((t0 + Thigh, 'fall'))
        edges_lo.append((t0 + Thigh + deadtime, 'rise'))
        edges_lo.append((t0 + T - deadtime, 'fall'))

    edges_hi.sort(key=lambda e: e[0])
    edges_lo.sort(key=lambda e: e[0])

    t_stop = cycles * T + extra_time
    pts_hi = build_channel(edges_hi, trise, tfall, vlow, vhigh, t_stop)
    pts_lo = build_channel(edges_lo, trise, tfall, vlow, vhigh, t_stop)
    return pts_hi, pts_lo, T, Thigh, Tlow


def write_pwl_file(path, points):
    with open(path, 'w') as f:
        for t, v in points:
            f.write(f"{t:.12e}\t{v:.6f}\n")


def points_to_inline_pwl(points, max_points=None):
    pts = points if max_points is None else points[:max_points]
    return "PWL(" + " ".join(f"{t:.9e} {v:.6f}" for t, v in pts) + ")"


def main():
    p = argparse.ArgumentParser(description="Generate complementary PWM PWL files for SPICE")
    p.add_argument('--freq', type=float, required=True, help='Switching frequency [Hz]')
    p.add_argument('--duty', type=float, required=True, help='Duty cycle of HIGH-side signal, 0-1')
    p.add_argument('--deadtime', type=float, required=True, help='Dead time between edges [s]')
    p.add_argument('--trise', type=float, default=None, help='Rise time [s] (overrides --rise-fall)')
    p.add_argument('--tfall', type=float, default=None, help='Fall time [s] (overrides --rise-fall)')
    p.add_argument('--rise-fall', type=float, default=10e-9,
                    help='Common rise/fall time [s] used if --trise/--tfall not given (default 10ns)')
    p.add_argument('--vhigh', type=float, default=1.0, help='High level [V] (default 1)')
    p.add_argument('--vlow', type=float, default=0.0, help='Low level [V] (default 0)')
    p.add_argument('--cycles', type=int, default=10, help='Number of periods to generate (default 10)')
    p.add_argument('--extra-time', type=float, default=0.0, help='Extra flat time appended at the end [s]')
    p.add_argument('--outdir', type=str, default='.', help='Output directory')
    p.add_argument('--prefix', type=str, default='pwm', help='Output filename prefix')
    args = p.parse_args()

    trise = args.trise if args.trise is not None else args.rise_fall
    tfall = args.tfall if args.tfall is not None else args.rise_fall

    pts_hi, pts_lo, T, Thigh, Tlow = generate(
        args.freq, args.duty, args.deadtime, trise, tfall,
        args.vhigh, args.vlow, args.cycles, args.extra_time
    )

    hi_path = f"{args.outdir}/{args.prefix}_high.pwl"
    lo_path = f"{args.outdir}/{args.prefix}_low.pwl"
    write_pwl_file(hi_path, pts_hi)
    write_pwl_file(lo_path, pts_lo)

    print(f"Period T        = {T*1e6:.4f} us  ({args.freq:.6g} Hz)")
    print(f"High time       = {Thigh*1e6:.4f} us  (duty = {args.duty*100:.2f}%)")
    print(f"Low time        = {Tlow*1e6:.4f} us")
    print(f"Deadtime        = {args.deadtime*1e9:.2f} ns (x2 per period)")
    print(f"Rise / Fall     = {trise*1e9:.2f} / {tfall*1e9:.2f} ns")
    print(f"Levels          = {args.vlow} V (low)  /  {args.vhigh} V (high)")
    print(f"Wrote {len(pts_hi)} points -> {hi_path}")
    print(f"Wrote {len(pts_lo)} points -> {lo_path}")
    print()
    print("SPICE usage (LTspice / ngspice), reading from file:")
    print(f'  VHI  gate_hi 0  PWL file="{hi_path}"')
    print(f'  VLO  gate_lo 0  PWL file="{lo_path}"')
    print()
    print("Or inline in the netlist (first points shown):")
    print("  VHI gate_hi 0", points_to_inline_pwl(pts_hi, max_points=8), " ...")


if __name__ == '__main__':
    main()