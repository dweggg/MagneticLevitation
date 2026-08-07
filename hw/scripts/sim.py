import numpy as np
from scipy.integrate import solve_ivp
from scipy import signal
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.animation as animation
from matplotlib.patches import Circle, Rectangle

# ============================================================================
# MAGLEV CLOSED-LOOP SIMULATOR
# ============================================================================
# Geometry: coil lies flat on the table, magnet floats ABOVE it.
#   x = height of the magnet above the coil's mid-plane (positive up)
#   gravity is always -g (down); the coil's field must push the magnet UP.
#   Current control is assumed instantaneous, but coil RESISTANCE (and hence
#   instantaneous power P=i^2*R) is derived from the real coil geometry.
#
# With the coil BELOW the magnet, Newton's law is
#     M*xddot = F(i,x) - M*g         where F(i,x) = i * Fpa(x)
# ============================================================================

# ---------------- CONSTANTS ----------------
mu0     = 4*np.pi*1e-7
rho_cu  = 1.72e-8
dens_cu = 8960.0
g       = 9.81

# ---------------- MAGNET ----------------
Br       = 1.32
mag_dia  = 0.025
mag_h    = 0.005
V_mag  = np.pi*(mag_dia/2)**2*mag_h
m_dip  = Br*V_mag/mu0

# ---------------- COIL ----------------
ID               = 0.060    # m, inner diameter
wire_awg         = 20
n_layers         = 9
turns_per_layer  = 10

M0 = 0.030    # kg, "starting weight" of the floating assembly
x0 = 0.050    # m, nominal hover height used for linearization


# ============================================================================
# ELECTRICAL LIMITS
# ============================================================================
I_max = 25.0
V_max = 20.0
P_max = 100.0

# ============================================================================
#   Phase 1: settle from an initial offset, at nominal mass
#   Phase 2: position setpoint change
#   Phase 3: weight disturbance (mass added/removed), same setpoint
#   Phase 4: another position setpoint change
# ============================================================================
x_initial_offset = -0.020    # m, start off and let the controller catch it
mass_disturbance = +0.100    # kg, add/remove at the start of phase 3

phases = [
    (2, 0.050, M0),
    (2, 0.02, M0),
    (2, 0.02, M0 + mass_disturbance),
    (2, 0.015, M0 + mass_disturbance),
]

# ============================================================================
# CONTROLLER TUNING 
# ============================================================================
# Closed loop (error e = x_target - x, di = Kp*e + Kd*edot + Ki*int(e dt)):
#   M*s^3 + B*Kd*s^2 + (B*Kp - A)*s + B*Ki = 0
# Desired poles: -p (real) and -zeta*wn +- j*wn*sqrt(1-zeta^2), with p = k*wn
zeta = 0.8      # damping ratio of the complex pole pair (design taste)
k    = 5.0      # real pole = k * wn, i.e. how much further left it sits (design taste)
wn   = 20.0     # <<< THE ONE TUNING KNOB: closed-loop bandwidth, rad/s >>>

def awg_to_diameter_m(awg, insulation_factor=1.07):
    d_bare = 0.127e-3 * 92**((36-awg)/39.0)
    return d_bare*insulation_factor, d_bare

d_wire, d_bare = awg_to_diameter_m(wire_awg)
R_in = ID/2
radii    = R_in + d_wire*(np.arange(n_layers)+0.5)
z_offset = d_wire*(np.arange(turns_per_layer) - (turns_per_layer-1)/2.0)
RR, ZZ = np.meshgrid(radii, z_offset, indexing='ij')
radii_flat = RR.ravel()
z_flat     = ZZ.ravel()
N = radii_flat.size
OD = 2*(R_in + n_layers*d_wire)
coil_height = turns_per_layer*d_wire
coil_top = coil_height / 2.0   # distance from coil center to top surface

wire_area    = np.pi*(d_bare/2)**2
total_length = float((2*np.pi*radii_flat).sum())
R_coil = rho_cu*total_length/wire_area              # <-- derived from geometry, used for power
copper_weight_g = total_length*wire_area*dens_cu*1000

print(f"Coil: {N} turns (AWG{wire_awg}), ID/OD {ID*1000:.0f}/{OD*1000:.1f} mm, "
      f"height {coil_height*1000:.1f} mm, R_coil={R_coil:.3f} ohm, "
      f"copper {copper_weight_g:.0f} g")

def force_per_amp(x):
    """
    Upward force per amp.
    x is height ABOVE THE TOP OF THE COIL.
    """
    x_center = x + coil_top
    x_k = x_center - z_flat
    denom = (radii_flat**2 + x_k**2)**2.5
    return (
        float((1.5*mu0*m_dip*(radii_flat**2)*x_k/denom).sum())
        if np.isscalar(x)
        else (1.5*mu0*m_dip*(radii_flat**2)*x_k/denom).sum()
    )

# ============================================================================
# NOMINAL OPERATING POINT
# ============================================================================

Fpa0 = force_per_amp(x0)
i0   = M0*g/Fpa0

eps = 1e-6
A = i0*(force_per_amp(x0+eps) - force_per_amp(x0-eps))/(2*eps)   # d(M*xddot)/dx
B = Fpa0                                                         # d(M*xddot)/di

print(f"\nOperating point: x0={x0*1000:.0f}mm, M0={M0*1000:.0f}g -> i0={i0:.3f} A")
print(f"A = {A:.3f} N/m   B = {B:.4f} N/A")

Kd = M0*wn*(k + 2*zeta)/B
Kp = (M0*wn**2*(1 + 2*zeta*k) + A)/B     # NOTE the "+A"
Ki = M0*k*wn**3/B

print(f"\nController gains (wn={wn} rad/s, zeta={zeta}, k={k}):")
print(f"  Kp = {Kp:.2f} A/m")
print(f"  Kd = {Kd:.4f} A/(m/s)")
print(f"  Ki = {Ki:.2f} A/(m*s)")

char_poly = [M0, B*Kd, B*Kp - A, B*Ki]
poles = np.roots(char_poly)
if np.all(poles.real < 0):
    print("\nAll poles have negative real parts -> closed loop is stable. Good.")
else:
    print("\n*** WARNING: an unstable pole was found -- do not trust this design! ***")

# ============================================================================
# LINEARIZED TRANSFER FUNCTIONS
# ============================================================================
#
# Plant:
#     G(s) = B / (M s^2 - A)
#
# PID:
#     C(s) = (Kd s^2 + Kp s + Ki) / s
#
# Open-loop:
#     L(s) = C(s)G(s)
# ============================================================================

# Plant
plant_num = [B]
plant_den = [M0, 0.0, -A]

# PID
pid_num = [Kd, Kp, Ki]
pid_den = [1.0, 0.0]

# Open-loop
ol_num = np.polymul(pid_num, plant_num)
ol_den = np.polymul(pid_den, plant_den)

# Open-loop poles/zeros
ol_poles = np.roots(ol_den)
ol_zeros = np.roots(ol_num)

# Closed-loop transfer function
cl_den = np.polyadd(ol_den, ol_num)
cl_num = ol_num

cl_poles = np.roots(cl_den)
cl_zeros = np.roots(cl_num)

print("\nOpen-loop poles:")
print(ol_poles)

print("\nOpen-loop zeros:")
print(ol_zeros)

print("\nClosed-loop poles:")
print(cl_poles)

print("\nClosed-loop zeros:")
print(cl_zeros)


# ============================================================================
# ROOT LOCUS
# ============================================================================

def poly_eval(coeffs, s):
    """Evaluate polynomial with descending-order coefficients."""
    return np.polyval(coeffs, s)

K_vals = np.linspace(0, 1.5, 300)

root_branches = []

for K in K_vals:
    den = np.polyadd(ol_den, K*np.asarray(ol_num))
    root_branches.append(np.roots(den))

root_branches = np.asarray(root_branches)

# Match branches for smooth curves
for k in range(1, len(K_vals)):
    prev = root_branches[k-1]
    curr = list(root_branches[k])
    ordered = []

    for p in prev:
        idx = np.argmin(np.abs(np.array(curr)-p))
        ordered.append(curr.pop(idx))

    root_branches[k] = ordered

plt.figure(figsize=(8,7))

# Root-locus branches
for i in range(root_branches.shape[1]):
    plt.plot(root_branches[:,i].real,
             root_branches[:,i].imag,
             'b', lw=1.5)

# Open-loop poles
plt.plot(ol_poles.real,
         ol_poles.imag,
         'rx',
         markersize=10,
         markeredgewidth=2,
         label='Open-loop poles')

# Open-loop zeros
if len(ol_zeros):
    plt.plot(ol_zeros.real,
             ol_zeros.imag,
             'go',
             markersize=9,
             fillstyle='none',
             markeredgewidth=2,
             label='Open-loop zeros')

# Closed-loop poles (your designed controller, K=1)
plt.plot(cl_poles.real,
         cl_poles.imag,
         'ks',
         markersize=8,
         label='Closed-loop poles')

# Closed-loop zeros
if len(cl_zeros):
    plt.plot(cl_zeros.real,
             cl_zeros.imag,
             'md',
             markersize=8,
             fillstyle='none',
             label='Closed-loop zeros')

plt.axhline(0,color='k',lw=0.8)
plt.axvline(0,color='k',lw=0.8)

plt.grid(True, alpha=0.3)
plt.xlabel("Real")
plt.ylabel("Imaginary")
plt.title("Root Locus")
plt.legend()
plt.axis('equal')

plt.savefig("outputs/maglev_root_locus.png", dpi=150)
print("Saved root locus to outputs/maglev_root_locus.png")

def make_rhs(x_target, M):
    def rhs(t, state):
        x, v, z = state
        e = x_target - x
        i_cmd = i0 + Kp*e + Kd*(-v) + Ki*z     # edot = -v when x_target is held constant
        i_clip = np.clip(i_cmd, -I_max, I_max)
        F = i_clip*force_per_amp(x)
        xdd = (F - M*g)/M
        return [v, xdd, e]
    return rhs

t_all, x_all, v_all, i_all, e_all, M_all, target_all = [], [], [], [], [], [], []
t_offset = 0.0
state = [x0 + x_initial_offset, 0.0, 0.0]

for phase_num, (dur, x_target, M) in enumerate(phases, start=1):
    rhs = make_rhs(x_target, M)
    t_eval = np.linspace(0, dur, 600)
    sol = solve_ivp(rhs, [0, dur], state, t_eval=t_eval, max_step=dur/2000, method='RK45')
    x_arr, v_arr, z_arr = sol.y
    e_arr = x_target - x_arr
    i_arr = np.clip(i0 + Kp*e_arr + Kd*(-v_arr) + Ki*z_arr, -I_max, I_max)

    n_ss = max(10, len(sol.t) // 10)
    sl = slice(-n_ss, None)

    x_ss = np.mean(x_arr[sl])
    i_ss = np.mean(i_arr[sl])
    P_ss = np.mean(i_arr[sl]**2 * R_coil)

    t_all.append(sol.t + t_offset)
    x_all.append(x_arr); v_all.append(v_arr); i_all.append(i_arr); e_all.append(e_arr)
    M_all.append(np.full_like(sol.t, M))
    target_all.append(np.full_like(sol.t, x_target))

    peak_i = np.max(np.abs(i_arr))
    
    print(f" Phase {phase_num}: target = {x_target*1000:6.1f} mm | M = {M*1000:6.1f} g | "
          f"steady I = {i_ss:6.3f} A | steady P = {P_ss:6.2f} W | peak |I| = {peak_i:6.2f} A")

    state = [x_arr[-1], v_arr[-1], z_arr[-1]]
    t_offset += dur

t_all = np.concatenate(t_all); x_all = np.concatenate(x_all); v_all = np.concatenate(v_all)
i_all = np.concatenate(i_all); e_all = np.concatenate(e_all)
M_all = np.concatenate(M_all); target_all = np.concatenate(target_all)
P_all = i_all**2*R_coil

# ============================================================================
# SENSOR MODEL: GH39FKSW LINEAR HALL SENSOR (AT BOTTOM OF COIL)
# ============================================================================
# 1. Position: on-axis, at the bottom surface of the coil (z = -coil_top)
z_sensor = -coil_top
d_k_sensor = z_sensor - z_flat

# 2. Coil's magnetic field at the sensor per Ampere of current
# Using the on-axis B-field formula for a current loop: B_z = (mu0*I*r^2) / (2*(r^2 + z^2)^1.5)
B_coil_per_amp = np.sum(mu0 * (radii_flat**2) / (2 * (radii_flat**2 + d_k_sensor**2)**1.5))

# 3. Magnet's magnetic field at the sensor
# Distance from magnet center to the sensor at the coil bottom is x_all + coil_height.
d_mag_sensor = x_all + coil_height
# The coil repels the magnet (pushing it UP). For positive coil current (B_coil UP), 
# the magnet's dipole must face DOWN. Hence, its field at the sensor points DOWN (negative).
B_mag_all = - (mu0 * m_dip) / (2 * np.pi * d_mag_sensor**3)

# 4. Total Magnetic Field (Tesla -> milliTesla)
B_sensor_T = (B_coil_per_amp * i_all) + B_mag_all
B_sensor_mT = B_sensor_T * 1000

# 5. Sensor Output Voltage (GH39FKSW fed at 3.3V)
# Specs: Sensitivity ~1.8 mV/Gs = 18 mV/mT. 
# Zero-field output is centered at Vcc/2 = 1.65V.
Vcc_sensor = 3.3
Vq_sensor = Vcc_sensor / 2.0
sensitivity_V_per_mT = 0.018  # 18 mV / mT

V_sensor_all = Vq_sensor + (B_sensor_mT * sensitivity_V_per_mT)
V_sensor_all = np.clip(V_sensor_all, 0.0, Vcc_sensor)  # Clamp output to supply rails

# ============================================================================
# STATIC PLOTS
# ============================================================================
fig, axes = plt.subplots(6, 1, figsize=(11, 15), sharex=True)

axes[0].plot(t_all, x_all*1000, label='magnet height x(t)', color='navy')
axes[0].plot(t_all, target_all*1000, '--', label='setpoint', color='crimson')
axes[0].set_ylabel('Position (mm)')
axes[0].legend(loc='upper right'); axes[0].grid(alpha=0.3)

axes[1].plot(t_all, v_all*1000, color='darkorange')
axes[1].set_ylabel('Velocity (mm/s)'); axes[1].grid(alpha=0.3)

axes[2].plot(t_all, i_all, color='seagreen')
axes[2].axhline(I_max, color='red', ls=':', lw=1, label='I_max')
axes[2].axhline(-I_max, color='red', ls=':', lw=1)
axes[2].set_ylabel('Current (A)'); axes[2].legend(loc='upper right'); axes[2].grid(alpha=0.3)

axes[3].plot(t_all, P_all, color='purple')
axes[3].axhline(P_max, color='red', ls=':', lw=1, label='P_max')
axes[3].set_ylabel('Power (W)'); axes[3].legend(loc='upper right'); axes[3].grid(alpha=0.3)

axes[4].plot(t_all, M_all*1000, color='saddlebrown')
axes[4].set_ylabel('Mass (g)'); axes[4].grid(alpha=0.3)

# --- SENSOR PLOT ---
axes[5].plot(t_all, V_sensor_all, color='teal', label='GH39FKSW Output (V)')
axes[5].axhline(Vq_sensor, color='gray', ls=':', lw=1, label=f'Zero-field ({Vq_sensor}V)')
axes[5].set_ylabel('Sensor (V)')
axes[5].set_xlabel('Time (s)')
axes[5].legend(loc='upper right'); axes[5].grid(alpha=0.3)

# Add a secondary axis to see the actual mT values matching the voltage
ax_mT = axes[5].twinx()
ax_mT.plot(t_all, B_sensor_mT, color='teal', alpha=0)  # Invisible, used just to scale axis
ax_mT.set_ylabel('B-field (mT)')
y1, y2 = axes[5].get_ylim()
ax_mT.set_ylim((y1 - Vq_sensor)/sensitivity_V_per_mT, (y2 - Vq_sensor)/sensitivity_V_per_mT)

boundaries = np.cumsum([p[0] for p in phases])[:-1]
for ax in axes:
    for b in boundaries:
        ax.axvline(b, color='gray', lw=0.8, alpha=0.6)

plt.tight_layout()
plt.savefig('outputs/maglev_sim_plots.png', dpi=150)
print("\nSaved plots to maglev_sim_plots.png")

# ============================================================================
# ANIMATION
# ============================================================================
fps = 30
total_T = t_all[-1]
n_frames = int(total_T*fps)
t_frames = np.linspace(0, total_T, n_frames)
x_frames = np.interp(t_frames, t_all, x_all)
i_frames = np.interp(t_frames, t_all, i_all)
tgt_frames = np.interp(t_frames, t_all, target_all)
M_frames = np.interp(t_frames, t_all, M_all)
V_sensor_frames = np.interp(t_frames, t_all, V_sensor_all)
B_sensor_frames = np.interp(t_frames, t_all, B_sensor_mT)

fig2, ax2 = plt.subplots(figsize=(4.5, 7))
y_max = max(x_all.max(), target_all.max())*1000 + 30
ax2.set_xlim(-60, 60)
ax2.set_ylim(-coil_height*1000 - 10, y_max)
ax2.set_aspect('equal')
ax2.set_xlabel('mm'); 
ax2.set_ylabel('Height above coil top (mm)')
ax2.set_title('Maglev simulation')

coil_rect = Rectangle(
    (-OD*1000/2, -coil_height*1000),
    OD*1000,
    coil_height*1000,
    facecolor='goldenrod',
    edgecolor='black',
    alpha=0.85,
    zorder=1,
)

ax2.add_patch(coil_rect)
ax2.text(0, -coil_height*1000-6, 'coil', ha='center', fontsize=9)

magnet_patch = Circle((0, x0*1000), mag_dia*1000/2, facecolor='crimson',
                       edgecolor='black', zorder=5)
ax2.add_patch(magnet_patch)

target_line = ax2.axhline(x0*1000, color='gray', ls='--', lw=1, alpha=0.7)
info_text = ax2.text(0.02, 0.98, '', transform=ax2.transAxes, va='top', fontsize=9,
                      family='monospace')

def init():
    return magnet_patch, target_line, info_text

def update(frame):
    magnet_patch.center = (0, x_frames[frame]*1000)
    target_line.set_ydata([tgt_frames[frame]*1000, tgt_frames[frame]*1000])
    info_text.set_text(f"t = {t_frames[frame]:5.2f} s\n"
                       f"x = {x_frames[frame]*1000:5.1f} mm\n"
                       f"tgt = {tgt_frames[frame]*1000:5.1f} mm\n"
                       f"i = {i_frames[frame]:5.2f} A\n"
                       f"M = {M_frames[frame]*1000:5.1f} g\n"
                       f"B_sens = {B_sensor_frames[frame]:5.1f} mT\n"
                       f"V_sens = {V_sensor_frames[frame]:4.2f} V")
    return magnet_patch, target_line, info_text

ani = animation.FuncAnimation(fig2, update, frames=n_frames, init_func=init,
                               blit=True, interval=1000/fps)
ani.save('outputs/maglev_sim_animation.gif',
         writer=animation.PillowWriter(fps=fps))
print("Saved animation to maglev_sim_animation.gif")